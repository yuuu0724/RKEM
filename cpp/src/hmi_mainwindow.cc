#include "hmi_mainwindow.h"
#include "hmi_ocr_detector.h"
#include "yolo_detector.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDoubleValidator>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMetaObject>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QTime>
#include <QVBoxLayout>

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <mutex>

#include <sys/wait.h>
#include <unistd.h>

namespace {
constexpr const char *kBg = "#071019";
constexpr const char *kCard = "#101B26";
constexpr const char *kCard2 = "#132130";
constexpr const char *kBorder = "#233342";
constexpr const char *kText = "#E8F1F8";
constexpr const char *kSubText = "#91A4B7";
constexpr const char *kGood = "#22C55E";
constexpr const char *kBad = "#EF4444";
constexpr const char *kWarn = "#F7B500";
constexpr const char *kBlue = "#1E9BFF";
constexpr const char *kCyan = "#22D3FF";
constexpr int kDetectionInputSize = 640;
constexpr float kYoloThreshold = 0.15f;
constexpr double kCenterVisionMin = 0.2;
constexpr double kCenterVisionMax = 0.8;
std::mutex g_detectorInitMutex;

struct RawFrameHeader {
    char magic[8];
    int width;
    int height;
    int type;
    int bytes;
};

QString metricText(int value)
{
    return QLocale(QLocale::Chinese).toString(value);
}

void writeHmiAlarmLog(const QString &eventType, const QString &detail)
{
    QDir().mkpath(QStringLiteral("logs"));
    std::ofstream out("logs/hmi_alarm.log", std::ios::app);
    if (!out.is_open()) {
        return;
    }

    const QString line = QStringLiteral("[%1] %2 %3\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
        .arg(eventType)
        .arg(detail);
    const QByteArray bytes = line.toUtf8();
    out.write(bytes.constData(), bytes.size());
}

bool decodeJpegFrameIfNeeded(const cv::Mat &frame, cv::Mat &bgr)
{
    if (frame.empty() || frame.type() != CV_8UC1 || frame.total() < 4) {
        return false;
    }

    const unsigned char *data = frame.ptr<unsigned char>(0);
    const bool looksLikeJpeg = data[0] == 0xff && data[1] == 0xd8;
    if (!looksLikeJpeg && frame.rows > 1) {
        return false;
    }

    cv::Mat encoded = frame.isContinuous() ? frame.reshape(1, 1) : frame.clone().reshape(1, 1);
    cv::Mat decoded = cv::imdecode(encoded, cv::IMREAD_COLOR);
    if (decoded.empty()) {
        return false;
    }

    bgr = decoded;
    return true;
}

bool convertCameraFrameToRgb(const cv::Mat &frame, cv::Mat &rgb)
{
    if (frame.empty()) {
        return false;
    }

    cv::Mat bgr;
    if (frame.channels() == 1) {
        if (!decodeJpegFrameIfNeeded(frame, bgr)) {
            cv::cvtColor(frame, bgr, cv::COLOR_GRAY2BGR);
        }
    } else if (frame.channels() == 2) {
        cv::cvtColor(frame, bgr, cv::COLOR_YUV2BGR_YUY2);
    } else if (frame.channels() == 3) {
        bgr = frame;
    } else if (frame.channels() == 4) {
        cv::cvtColor(frame, bgr, cv::COLOR_BGRA2BGR);
    } else {
        return false;
    }

    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return true;
}

pid_t startCameraCaptureService(const QString &devicePath, const QString &outputPath)
{
    unlink(outputPath.toLocal8Bit().constData());
    const QString tmpOutputPath = outputPath + QStringLiteral(".tmp");
    unlink(tmpOutputPath.toLocal8Bit().constData());

    const QByteArray device = devicePath.toLocal8Bit();
    const QByteArray output = outputPath.toLocal8Bit();
    pid_t pid = fork();
    if (pid < 0) {
        std::perror("fork camera_capture_service");
        return -1;
    }
    if (pid == 0) {
        execl("./camera_capture_service", "camera_capture_service",
              "--camera", device.constData(),
              "--output", output.constData(),
              "--width", "640",
              "--height", "480",
              static_cast<char *>(nullptr));
        execlp("camera_capture_service", "camera_capture_service",
               "--camera", device.constData(),
               "--output", output.constData(),
               "--width", "640",
               "--height", "480",
               static_cast<char *>(nullptr));
        std::perror("exec camera_capture_service");
        _exit(127);
    }
    return pid;
}

void stopCameraCaptureService(int pid)
{
    if (pid <= 0) {
        return;
    }
    kill(pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        int status = 0;
        const pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

bool readCaptureServiceFrame(const QString &framePath, cv::Mat &frame)
{
    std::ifstream in(framePath.toLocal8Bit().constData(), std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    RawFrameHeader header;
    if (!in.read(reinterpret_cast<char *>(&header), sizeof(header))) {
        return false;
    }
    if (std::string(header.magic, header.magic + 7) != "IIFRM01" ||
        header.width <= 0 || header.height <= 0 || header.bytes <= 0) {
        return false;
    }

    cv::Mat image(header.height, header.width, header.type);
    const size_t expectedBytes = image.total() * image.elemSize();
    if (header.bytes != static_cast<int>(expectedBytes)) {
        return false;
    }
    if (!in.read(reinterpret_cast<char *>(image.data), expectedBytes)) {
        return false;
    }

    frame = image;
    return true;
}

QString defectLabelText(const QString &label)
{
    const QString normalized = label.trimmed().toLower();
    if (normalized == QStringLiteral("pin_damage") ||
        normalized == QStringLiteral("pin_missing") ||
        normalized.contains(QStringLiteral("pin"))) {
        return QStringLiteral("引脚损坏");
    }
    if (normalized == QStringLiteral("scratch") ||
        normalized.contains(QStringLiteral("scratch"))) {
        return QStringLiteral("划痕");
    }
    return label;
}

QString defectReasonText(const QString &defectSummary)
{
    QStringList defects;
    const QString normalized = defectSummary.toLower();
    if (normalized.contains(QStringLiteral("pin_damage")) ||
        normalized.contains(QStringLiteral("pin_missing")) ||
        normalized.contains(QStringLiteral("pin")) ||
        defectSummary.contains(QStringLiteral("引脚"))) {
        defects.append(QStringLiteral("引脚损坏"));
    }
    if (normalized.contains(QStringLiteral("scratch")) ||
        defectSummary.contains(QStringLiteral("划痕"))) {
        defects.append(QStringLiteral("划痕"));
    }
    defects.removeDuplicates();
    return defects.isEmpty() ? defectSummary : defects.join(QStringLiteral("；"));
}

QString wrapSlotValue(const QString &value, int maxLineLength = 16)
{
    if (value.isEmpty() || maxLineLength <= 0) {
        return value;
    }

    QString wrapped;
    int lineLength = 0;
    for (const QChar ch : value) {
        if (ch == QLatin1Char('\n')) {
            wrapped.append(ch);
            lineLength = 0;
            continue;
        }
        if (lineLength >= maxLineLength) {
            wrapped.append(QLatin1Char('\n'));
            lineLength = 0;
        }
        wrapped.append(ch);
        ++lineLength;
    }
    return wrapped;
}
}

CameraPreviewWidget::CameraPreviewWidget(const QString &cameraName,
                                         const QString &devicePath,
                                         DetectionMode detectionMode,
                                         QWidget *parent)
    : QWidget(parent),
      m_cameraName(cameraName),
      m_devicePath(devicePath),
      m_message(QStringLiteral("正在打开摄像头：%1").arg(devicePath)),
      m_fatigueText(QStringLiteral("疲劳状态：等待数据")),
      m_frameTimer(new QTimer(this)),
      m_capture(nullptr),
      m_captureFramePath(QStringLiteral("/tmp/integrated_inspection_hmi_%1.bgr").arg(devicePath.section('/', -1))),
      m_online(false),
      m_fatigueOverlay(detectionMode == DetectionMode::Fatigue),
      m_detectionMode(detectionMode),
      m_detectionRunning(false)
{
    setMinimumHeight(118);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(m_frameTimer, &QTimer::timeout, this, &CameraPreviewWidget::readFrame);
    startCamera();
    startDetection();
}

CameraPreviewWidget::~CameraPreviewWidget()
{
    stopDetection();
    stopCamera();
}

bool CameraPreviewWidget::startCamera(int width, int height)
{
    stopCamera();

    if (m_devicePath == QStringLiteral("/dev/video21")) {
        m_captureServicePid = startCameraCaptureService(m_devicePath, m_captureFramePath);
        if (m_captureServicePid <= 0) {
            m_online = false;
            m_message = QStringLiteral("摄像头采集服务启动失败：%1").arg(m_devicePath);
            update();
            return false;
        }
        m_online = true;
        m_message = QStringLiteral("实时画面：%1").arg(m_devicePath);
        m_frameTimer->start(33);
        readFrame();
        return true;
    }

    cv::VideoCapture *capture = new cv::VideoCapture;
    const bool opened = capture->open(m_devicePath.toStdString(), cv::CAP_V4L2);
    if (!opened || !capture->isOpened()) {
        delete capture;
        m_capture = nullptr;
        m_online = false;
        m_message = QStringLiteral("摄像头打开失败：%1").arg(m_devicePath);
        update();
        return false;
    }

    const int fourcc = (m_devicePath == QStringLiteral("/dev/video23"))
        ? cv::VideoWriter::fourcc('Y', 'U', 'Y', 'V')
        : cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    capture->set(cv::CAP_PROP_FOURCC, fourcc);
    capture->set(cv::CAP_PROP_FRAME_WIDTH, width);
    capture->set(cv::CAP_PROP_FRAME_HEIGHT, height);
    capture->set(cv::CAP_PROP_FPS, 30);
    capture->set(cv::CAP_PROP_BUFFERSIZE, 1);

    m_capture = capture;
    m_online = true;
    m_message = QStringLiteral("实时画面：%1").arg(m_devicePath);
    m_frameTimer->start(33);
    readFrame();
    return true;
}

void CameraPreviewWidget::stopCamera()
{
    if (m_frameTimer) {
        m_frameTimer->stop();
    }

    if (m_captureServicePid > 0) {
        stopCameraCaptureService(m_captureServicePid);
        m_captureServicePid = -1;
    }

    cv::VideoCapture *capture = static_cast<cv::VideoCapture *>(m_capture);
    if (capture) {
        if (capture->isOpened()) {
            capture->release();
        }
        delete capture;
        m_capture = nullptr;
    }
    m_online = false;
}

void CameraPreviewWidget::setFrame(const QImage &frame)
{
    m_frame = frame.convertToFormat(QImage::Format_RGB888);
    if (!m_frame.isNull()) {
        cv::Mat rgb(m_frame.height(),
                    m_frame.width(),
                    CV_8UC3,
                    const_cast<uchar *>(m_frame.constBits()),
                    static_cast<size_t>(m_frame.bytesPerLine()));
        updateInferenceFrame(rgb);
    }
    m_online = !m_frame.isNull();
    if (m_online) {
        m_message = QStringLiteral("实时画面：%1").arg(m_devicePath);
    }
    update();
}

void CameraPreviewWidget::setOnline(bool online)
{
    m_online = online;
    if (!online && m_frame.isNull()) {
        m_message = QStringLiteral("摄像头打开失败或未接入：%1").arg(m_devicePath);
    }
    update();
}

void CameraPreviewWidget::setMessage(const QString &message)
{
    m_message = message;
    update();
}

void CameraPreviewWidget::setFatigueText(const QString &text)
{
    std::lock_guard<std::mutex> lock(m_fatigueMutex);
    m_fatigueText = text.isEmpty() ? QStringLiteral("状态：清醒") : text;
    update();
}

void CameraPreviewWidget::readFrame()
{
    cv::Mat frame;
    if (m_captureServicePid > 0) {
        if (!readCaptureServiceFrame(m_captureFramePath, frame) || frame.empty()) {
            m_online = false;
            m_message = QStringLiteral("等待摄像头采集服务：%1").arg(m_devicePath);
            update();
            return;
        }
    } else {
        cv::VideoCapture *capture = static_cast<cv::VideoCapture *>(m_capture);
        if (!capture || !capture->isOpened()) {
            m_online = false;
            m_message = QStringLiteral("摄像头未连接：%1").arg(m_devicePath);
            update();
            return;
        }

        if (!capture->read(frame) || frame.empty()) {
            m_online = false;
            m_message = QStringLiteral("摄像头读取失败：%1").arg(m_devicePath);
            update();
            return;
        }
    }

    cv::Mat rgb;
    if (!convertCameraFrameToRgb(frame, rgb)) {
        m_online = false;
        m_message = QStringLiteral("摄像头格式转换失败：%1").arg(m_devicePath);
        update();
        return;
    }

    m_frame = QImage(rgb.data,
                     rgb.cols,
                     rgb.rows,
                     static_cast<int>(rgb.step),
                     QImage::Format_RGB888).copy();
    updateInferenceFrame(rgb);
    m_online = true;
    m_message = QStringLiteral("实时画面：%1").arg(m_devicePath);
    update();
}

QRect CameraPreviewWidget::imageRect(const QRect &targetRect, const QSize &imageSize) const
{
    if (imageSize.isEmpty()) {
        return targetRect;
    }

    QSize scaled = imageSize;
    scaled.scale(targetRect.size(), Qt::KeepAspectRatio);
    return QRect(QPoint(targetRect.center().x() - scaled.width() / 2,
                       targetRect.center().y() - scaled.height() / 2),
                 scaled);
}

void CameraPreviewWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect area = rect().adjusted(1, 1, -1, -1);
    painter.fillRect(area, QColor(kBg));

    if (!m_frame.isNull()) {
        const QRect imageArea = imageRect(area, m_frame.size());
        painter.drawImage(imageArea, m_frame);
        drawDetectionOverlay(painter, imageArea);
    } else {
        drawPlaceholder(painter, area);
    }

    painter.setPen(QPen(QColor(m_online ? kGood : kWarn), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(area, 7, 7);

    QRect badge(area.left() + 8, area.top() + 7, std::min(area.width() - 16, 240), 24);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(7, 16, 25, 220));
    painter.drawRoundedRect(badge, 4, 4);
    painter.setPen(QColor(m_online ? kGood : kWarn));
    QFont font = painter.font();
    font.setPixelSize(std::max(10, height() / 15));
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(badge.adjusted(8, 0, -8, 0),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     m_online ? m_cameraName : m_message);

    if (m_fatigueOverlay) {
        drawFatigueOverlay(painter, area);
    }
}

void CameraPreviewWidget::drawPlaceholder(QPainter &painter, const QRect &area)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#0B1622"));
    painter.drawRoundedRect(area, 7, 7);

    painter.setPen(QPen(QColor("#1B3244"), 1));
    for (int y = area.top() + 18; y < area.bottom(); y += 22) {
        painter.drawLine(area.left() + 12, y, area.right() - 12, y);
    }
    for (int x = area.left() + 18; x < area.right(); x += 28) {
        painter.drawLine(x, area.top() + 12, x, area.bottom() - 12);
    }

    QFont font = painter.font();
    font.setPixelSize(std::max(12, height() / 12));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor(kSubText));
    painter.drawText(area.adjusted(16, 18, -16, -18), Qt::AlignCenter | Qt::TextWordWrap, m_message);
}

void CameraPreviewWidget::drawFatigueOverlay(QPainter &painter, const QRect &area)
{
    QString fatigueText;
    FatigueLevel fatigueLevel = FatigueLevel::Awake;
    int fatigueScore = 0;
    {
        std::lock_guard<std::mutex> lock(m_fatigueMutex);
        fatigueText = m_fatigueText;
        fatigueLevel = m_fatigueLevel;
        fatigueScore = m_fatigueScore;
    }
    if (fatigueText.isEmpty()) {
        fatigueText = QStringLiteral("状态：清醒");
    }

    const bool severeBlinkOn = fatigueLevel == FatigueLevel::Severe &&
        (QDateTime::currentMSecsSinceEpoch() / 350) % 2 == 0;
    const QColor statusColor = fatigueLevel == FatigueLevel::Severe
        ? (severeBlinkOn ? QColor("#FF2D2D") : QColor(kBad))
        : (fatigueLevel == FatigueLevel::Fatigue ? QColor(kWarn) : QColor(kGood));
    const QColor panelColor = fatigueLevel == FatigueLevel::Severe
        ? (severeBlinkOn ? QColor(96, 8, 14, 245) : QColor(32, 4, 8, 230))
        : QColor(7, 16, 25, 220);

    const QRect panel(area.left() + 10, area.bottom() - 42, area.width() - 20, 30);
    painter.setPen(Qt::NoPen);
    painter.setBrush(panelColor);
    painter.drawRoundedRect(panel, 5, 5);
    if (fatigueLevel == FatigueLevel::Severe) {
        painter.setPen(QPen(statusColor, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(panel.adjusted(1, 1, -1, -1), 5, 5);
    }

    const QRect bar(panel.left() + 10, panel.top() + 7, panel.width() - 20, 8);
    QRect active = bar;
    const int percent = fatigueLevel == FatigueLevel::Severe
        ? 100
        : (fatigueLevel == FatigueLevel::Fatigue ? std::min(90, 50 + fatigueScore * 15) : 30);
    active.setWidth(bar.width() * percent / 100);
    QRect inactive(active.right(), bar.top(), std::max(0, bar.right() - active.right()), bar.height());

    painter.setPen(Qt::NoPen);
    painter.setBrush(statusColor);
    painter.drawRoundedRect(active, 4, 4);
    painter.setBrush(QColor("#243241"));
    painter.drawRoundedRect(inactive, 4, 4);

    QFont font = painter.font();
    font.setPixelSize(std::max(8, height() / 18));
    font.setBold(fatigueLevel != FatigueLevel::Awake);
    painter.setFont(font);
    painter.setPen(fatigueLevel == FatigueLevel::Awake ? QColor(kSubText) : statusColor);
    painter.drawText(panel.adjusted(10, 13, -10, 0), Qt::AlignCenter, fatigueText);
}

void CameraPreviewWidget::drawDetectionOverlay(QPainter &painter, const QRect &imageArea)
{
    QVector<DetectionOverlay> overlays;
    bool ready = false;
    {
        std::lock_guard<std::mutex> lock(m_overlayMutex);
        overlays = m_detectionOverlays;
        ready = m_detectionReady;
    }

    if (m_detectionMode == DetectionMode::None || !ready) {
        return;
    }

    const QColor boxColor = m_detectionMode == DetectionMode::Defect ? QColor(kBad) : QColor(kWarn);
    painter.save();
    painter.setClipRect(imageArea);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font = painter.font();
    font.setPixelSize(std::max(9, height() / 17));
    font.setBold(true);
    painter.setFont(font);

    for (const DetectionOverlay &overlay : overlays) {
        QRectF box(imageArea.left() + overlay.modelBox.left() * imageArea.width() / kDetectionInputSize,
                   imageArea.top() + overlay.modelBox.top() * imageArea.height() / kDetectionInputSize,
                   overlay.modelBox.width() * imageArea.width() / kDetectionInputSize,
                   overlay.modelBox.height() * imageArea.height() / kDetectionInputSize);
        box = box.intersected(QRectF(imageArea));
        if (box.width() < 2.0 || box.height() < 2.0) {
            continue;
        }

        painter.setPen(QPen(boxColor, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(box);

        const QString text = QStringLiteral("%1 %2%")
            .arg(overlay.label)
            .arg(overlay.score * 100.0f, 0, 'f', 1);
        const QRect textBounds = painter.fontMetrics().boundingRect(text).adjusted(-5, -3, 5, 3);
        QRectF labelRect(box.left(), box.top() - textBounds.height() - 2,
                         std::min<double>(textBounds.width(), imageArea.width()),
                         textBounds.height());
        if (labelRect.top() < imageArea.top()) {
            labelRect.moveTop(box.top() + 2);
        }
        if (labelRect.right() > imageArea.right()) {
            labelRect.moveRight(imageArea.right());
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(boxColor.red(), boxColor.green(), boxColor.blue(), 220));
        painter.drawRoundedRect(labelRect, 3, 3);
        painter.setPen(QColor("#071019"));
        painter.drawText(labelRect.adjusted(5, 0, -5, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    }

    painter.restore();
}

void CameraPreviewWidget::updateInferenceFrame(const cv::Mat &rgb)
{
    if (rgb.empty()) {
        return;
    }

    cv::Mat gray;
    cv::cvtColor(rgb, gray, cv::COLOR_RGB2GRAY);
    cv::resize(gray, gray, cv::Size(kDetectionInputSize, kDetectionInputSize));

    std::lock_guard<std::mutex> lock(m_inferenceMutex);
    m_inferenceGray = gray.clone();
    ++m_inferenceSeq;
}

bool CameraPreviewWidget::latestGrayFrame(cv::Mat &gray) const
{
    std::lock_guard<std::mutex> lock(m_inferenceMutex);
    if (m_inferenceGray.empty()) {
        return false;
    }
    gray = m_inferenceGray.clone();
    return true;
}

bool CameraPreviewWidget::hasActiveDetections() const
{
    std::lock_guard<std::mutex> lock(m_overlayMutex);
    return m_detectionReady && !m_detectionOverlays.isEmpty();
}

QString CameraPreviewWidget::latestDetectionSummary() const
{
    std::lock_guard<std::mutex> lock(m_overlayMutex);
    if (!m_detectionReady || m_detectionOverlays.isEmpty()) {
        return QStringLiteral("--");
    }

    QStringList parts;
    for (const DetectionOverlay &overlay : m_detectionOverlays) {
        parts.append(QStringLiteral("%1 %.1f%% [%2,%3,%4,%5]")
                         .arg(defectLabelText(overlay.label))
                         .arg(overlay.score * 100.0f, 0, 'f', 1)
                         .arg(static_cast<int>(overlay.modelBox.left()))
                         .arg(static_cast<int>(overlay.modelBox.top()))
                         .arg(static_cast<int>(overlay.modelBox.width()))
                         .arg(static_cast<int>(overlay.modelBox.height())));
    }
    return parts.join(QStringLiteral("; "));
}

bool CameraPreviewWidget::isCenterVisionDetection(const QRectF &modelBox) const
{
    const double centerX = (modelBox.left() + modelBox.right()) / 2.0 / kDetectionInputSize;
    const double centerY = (modelBox.top() + modelBox.bottom()) / 2.0 / kDetectionInputSize;
    return centerX >= kCenterVisionMin && centerX <= kCenterVisionMax &&
        centerY >= kCenterVisionMin && centerY <= kCenterVisionMax;
}

bool CameraPreviewWidget::hasDetectionLabel(const QVector<DetectionOverlay> &overlays,
                                            const QString &label) const
{
    for (const DetectionOverlay &overlay : overlays) {
        if (overlay.label == label && isCenterVisionDetection(overlay.modelBox)) {
            return true;
        }
    }
    return false;
}

QString CameraPreviewWidget::fatigueLevelText(FatigueLevel level) const
{
    if (level == FatigueLevel::Severe) {
        return QStringLiteral("严重疲劳");
    }
    if (level == FatigueLevel::Fatigue) {
        return QStringLiteral("疲劳");
    }
    return QStringLiteral("清醒");
}

void CameraPreviewWidget::setFatigueDecision(FatigueLevel level,
                                             const QString &triggerText,
                                             int fatigueScore)
{
    QString text = QStringLiteral("状态：%1").arg(fatigueLevelText(level));
    if (!triggerText.isEmpty()) {
        text += QStringLiteral(" 触发时间：%1").arg(triggerText);
    }

    bool shouldBeep = false;
    std::lock_guard<std::mutex> lock(m_fatigueMutex);
    shouldBeep = level != m_fatigueLevel && level != FatigueLevel::Awake;
    m_fatigueLevel = level;
    m_fatigueText = text;
    m_fatigueScore = fatigueScore;
    if (shouldBeep) {
        writeHmiAlarmLog(fatigueLevelText(level), fatigueLevelText(level));
        QMetaObject::invokeMethod(qApp, []() {
            QApplication::beep();
        }, Qt::QueuedConnection);
    }
}

void CameraPreviewWidget::resetFatigueWarning()
{
    writeHmiAlarmLog(QStringLiteral("疲劳报警解除"), QStringLiteral("清醒"));
    m_fatigueSamples.clear();
    m_closedEyeEvents.clear();
    m_yawnEvents.clear();
    m_currentClosed = false;
    m_currentMouthOpen = false;
    m_currentFatigue = false;
    m_currentClosedEventCounted = false;
    m_currentMouthOpenCounted = false;
    setFatigueDecision(FatigueLevel::Awake, QString(), 0);
}

void CameraPreviewWidget::updateFatigueDecision(const QVector<DetectionOverlay> &overlays,
                                                const std::chrono::steady_clock::time_point &now)
{
    if (m_detectionMode != DetectionMode::Fatigue) {
        return;
    }

    const bool closedNow = hasDetectionLabel(overlays, QStringLiteral("close_eye"));
    const bool mouthOpenNow = hasDetectionLabel(overlays, QStringLiteral("open_mouth"));

    m_fatigueSamples.push_back({now, closedNow});
    while (!m_fatigueSamples.empty() &&
           now - m_fatigueSamples.front().time > std::chrono::seconds(60)) {
        m_fatigueSamples.pop_front();
    }

    if (closedNow && !m_currentClosed) {
        m_currentClosed = true;
        m_currentClosedEventCounted = false;
        m_closedStart = now;
    } else if (!closedNow && m_currentClosed) {
        m_currentClosed = false;
        m_currentClosedEventCounted = false;
    }
    if (m_currentClosed && !m_currentClosedEventCounted &&
        now - m_closedStart >= std::chrono::milliseconds(400)) {
        m_closedEyeEvents.push_back(now);
        m_currentClosedEventCounted = true;
    }

    if (mouthOpenNow && !m_currentMouthOpen) {
        m_currentMouthOpen = true;
        m_currentMouthOpenCounted = false;
        m_mouthOpenStart = now;
    } else if (!mouthOpenNow && m_currentMouthOpen) {
        m_currentMouthOpen = false;
        m_currentMouthOpenCounted = false;
    }
    if (m_currentMouthOpen && !m_currentMouthOpenCounted &&
        now - m_mouthOpenStart >= std::chrono::seconds(1)) {
        m_yawnEvents.push_back(now);
        m_currentMouthOpenCounted = true;
    }

    const auto fiveMinutes = std::chrono::minutes(5);
    while (!m_closedEyeEvents.empty() && now - m_closedEyeEvents.front() > fiveMinutes) {
        m_closedEyeEvents.pop_front();
    }
    while (!m_yawnEvents.empty() && now - m_yawnEvents.front() > fiveMinutes) {
        m_yawnEvents.pop_front();
    }

    int closedFrameCount = 0;
    for (const FatigueSample &sample : m_fatigueSamples) {
        if (sample.closed) {
            ++closedFrameCount;
        }
    }
    const double perclos = m_fatigueSamples.empty()
        ? 0.0
        : static_cast<double>(closedFrameCount) / static_cast<double>(m_fatigueSamples.size());

    int fatigueScore = static_cast<int>(m_closedEyeEvents.size() + m_yawnEvents.size());
    if (m_yawnEvents.size() >= 2) {
        ++fatigueScore;
    }
    const bool perclosFatigue = perclos >= 0.30;
    const bool scoreFatigue = fatigueScore >= 2;
    const bool fatigueNow = perclosFatigue || scoreFatigue;

    if (fatigueNow && !m_currentFatigue) {
        m_currentFatigue = true;
        m_fatigueStart = now;
    } else if (!fatigueNow && m_currentFatigue) {
        m_currentFatigue = false;
    }

    FatigueLevel level = FatigueLevel::Awake;
    if (fatigueNow) {
        level = m_currentFatigue && now - m_fatigueStart >= std::chrono::seconds(10)
            ? FatigueLevel::Severe
            : FatigueLevel::Fatigue;
    }

    if (level != FatigueLevel::Awake) {
        FatigueLevel previousLevel = FatigueLevel::Awake;
        {
            std::lock_guard<std::mutex> lock(m_fatigueMutex);
            previousLevel = m_fatigueLevel;
        }
        if (previousLevel == FatigueLevel::Awake) {
            m_warningStart = now;
        } else if (now - m_warningStart >= std::chrono::seconds(30)) {
            resetFatigueWarning();
            return;
        }
    }

    QString triggerTime;
    if (level != FatigueLevel::Awake) {
        triggerTime = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    }
    setFatigueDecision(level, triggerTime, fatigueScore);
}

void CameraPreviewWidget::startDetection()
{
    if (m_detectionMode == DetectionMode::None || m_detectionRunning.load()) {
        return;
    }

    m_detectionRunning.store(true);
    m_detectionThread = std::thread(&CameraPreviewWidget::detectionLoop, this);
}

void CameraPreviewWidget::stopDetection()
{
    m_detectionRunning.store(false);
    if (m_detectionThread.joinable()) {
        m_detectionThread.join();
    }
}

void CameraPreviewWidget::detectionLoop()
{
    InspectionYoloDetector detector;
    bool initialized = false;
    {
        std::lock_guard<std::mutex> lock(g_detectorInitMutex);
        if (m_detectionMode == DetectionMode::Defect) {
            initialized = detector.init("model/defect/defect_best_i8.rknn",
                                        "model/defect/dataset.txt",
                                        2,
                                        kYoloThreshold,
                                        RKNN_NPU_CORE_2);
        } else if (m_detectionMode == DetectionMode::Fatigue) {
            initialized = detector.init("model/fatigue/fatigue_two_outputs_i8.rknn",
                                        "model/fatigue/dataset.txt",
                                        3,
                                        kYoloThreshold,
                                        RKNN_NPU_CORE_1);
        }
    }

    if (!initialized) {
        std::fprintf(stderr, "[ERROR] HMI detection init failed for %s\n", m_devicePath.toStdString().c_str());
        return;
    }

    uint64_t lastSeq = 0;
    uint64_t inferCount = 0;
    size_t lastDetectionCount = 0;
    auto lastLog = std::chrono::steady_clock::now();
    const char *modeName = m_detectionMode == DetectionMode::Defect ? "defect" : "fatigue";
    while (m_detectionRunning.load()) {
        cv::Mat gray;
        {
            std::lock_guard<std::mutex> lock(m_inferenceMutex);
            if (m_inferenceSeq != lastSeq && !m_inferenceGray.empty()) {
                lastSeq = m_inferenceSeq;
                gray = m_inferenceGray.clone();
            }
        }

        if (gray.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::vector<YoloDetection> detections;
        if (!detector.detect(gray, detections)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        ++inferCount;
        lastDetectionCount = detections.size();

        QVector<DetectionOverlay> overlays;
        overlays.reserve(static_cast<int>(detections.size()));
        for (const YoloDetection &detection : detections) {
            DetectionOverlay overlay;
            overlay.modelBox = QRectF(detection.box.x,
                                      detection.box.y,
                                      detection.box.width,
                                      detection.box.height);
            overlay.label = QString::fromStdString(detector.label(detection.classId));
            overlay.score = detection.score;
            if (m_detectionMode == DetectionMode::Fatigue &&
                !isCenterVisionDetection(overlay.modelBox)) {
                continue;
            }
            overlays.push_back(overlay);
        }
        if (m_detectionMode == DetectionMode::Fatigue) {
            updateFatigueDecision(overlays, std::chrono::steady_clock::now());
        }

        {
            std::lock_guard<std::mutex> lock(m_overlayMutex);
            m_detectionOverlays = overlays;
            m_detectionReady = true;
        }

        auto now = std::chrono::steady_clock::now();
        if (now - lastLog >= std::chrono::seconds(2)) {
            std::fprintf(stdout,
                         "[INFO] HMI %s infer frames=%llu detections=%zu camera=%s\n",
                         modeName,
                         static_cast<unsigned long long>(inferCount),
                         lastDetectionCount,
                         m_devicePath.toStdString().c_str());
            std::fflush(stdout);
            lastLog = now;
        }
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_clockTimer(new QTimer(this))
{
    buildUi();
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::refreshClock);
    refreshClock();
    m_clockTimer->start(1000);
    startOcrThread();
}

MainWindow::~MainWindow()
{
    stopOcrThread();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateScale();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::refreshClock()
{
    if (m_timeLabel) {
        m_timeLabel->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    }
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("芯片视觉检测与员工管理系统"));
    resize(DesignWidth, DesignHeight);
    setMinimumSize(900, 506);

    QWidget *central = new QWidget(this);
    central->setObjectName(QStringLiteral("windowRoot"));
    setCentralWidget(central);

    QGridLayout *outer = new QGridLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_canvas = new QWidget(central);
    m_canvas->setObjectName(QStringLiteral("canvas"));
    outer->addWidget(m_canvas, 0, 0, Qt::AlignCenter);

    QVBoxLayout *root = new QVBoxLayout(m_canvas);
    root->setContentsMargins(px(8), px(8), px(8), px(8));
    root->setSpacing(px(8));

    m_header = buildHeader();
    root->addWidget(m_header);

    QWidget *body = new QWidget(m_canvas);
    QHBoxLayout *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(px(8));

    QWidget *center = new QWidget(body);
    QVBoxLayout *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(px(8));
    centerLayout->addWidget(buildKpiCards(), 0);
    centerLayout->addWidget(buildCameraArea(), 1);
    centerLayout->addWidget(buildSlotResultArea(), 1);
    bodyLayout->addWidget(center, 1);

    m_rightPanel = buildRightPanel();
    bodyLayout->addWidget(m_rightPanel);
    root->addWidget(body, 1);

    updateScale();
    applyTheme();
}

QWidget *MainWindow::buildHeader()
{
    QFrame *bar = createCard(QStringLiteral("header"));
    bar->setFixedHeight(px(48));

    QLabel *title = createLabel(QStringLiteral("芯片视觉检测与员工管理系统"), QStringLiteral("headerTitle"));
    title->setAlignment(Qt::AlignCenter);

    m_timeLabel = createLabel(QStringLiteral("--"), QStringLiteral("headerTime"));
    m_timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    QGridLayout *layout = new QGridLayout(bar);
    layout->setContentsMargins(px(12), 0, px(12), 0);
    layout->setSpacing(0);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(2, 1);
    layout->addWidget(title, 0, 0, 1, 3, Qt::AlignCenter);
    layout->addWidget(m_timeLabel, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
    return bar;
}

QWidget *MainWindow::buildKpiCards()
{
    QWidget *area = new QWidget(m_canvas);
    QHBoxLayout *layout = new QHBoxLayout(area);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(px(8));
    layout->addWidget(createMetricCard(QStringLiteral("今日检测总数"), QStringLiteral("kpiTotal")));
    layout->addWidget(createMetricCard(QStringLiteral("良品数量"), QStringLiteral("kpiGood")));
    layout->addWidget(createMetricCard(QStringLiteral("次品数量"), QStringLiteral("kpiBad")));
    layout->addWidget(createMetricCard(QStringLiteral("良品率"), QStringLiteral("kpiRate")));
    return area;
}

QWidget *MainWindow::buildCameraArea()
{
    QWidget *area = new QWidget(m_canvas);
    QHBoxLayout *layout = new QHBoxLayout(area);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(px(8));

    QFrame *chipCard = createCard();
    QVBoxLayout *chipLayout = new QVBoxLayout(chipCard);
    chipLayout->setContentsMargins(px(10), px(8), px(10), px(10));
    chipLayout->setSpacing(px(4));
    chipLayout->addWidget(createLabel(QStringLiteral("摄像头 1：字符识别 + 缺陷检测"), QStringLiteral("cardTitle")));
    chipLayout->addWidget(createLabel(QStringLiteral("字符识别和缺陷检测共用 /dev/video21"), QStringLiteral("secondaryText")));
    m_camera1Preview = new CameraPreviewWidget(QStringLiteral("摄像头 1：/dev/video21"),
                                               QStringLiteral("/dev/video21"),
                                               CameraPreviewWidget::DetectionMode::Defect,
                                               chipCard);
    chipLayout->addWidget(m_camera1Preview, 1);

    QFrame *fatigueCard = createCard();
    QVBoxLayout *fatigueLayout = new QVBoxLayout(fatigueCard);
    fatigueLayout->setContentsMargins(px(10), px(8), px(10), px(10));
    fatigueLayout->setSpacing(px(4));
    fatigueLayout->addWidget(createLabel(QStringLiteral("摄像头 2：员工疲劳检测"), QStringLiteral("cardTitle")));
    fatigueLayout->addWidget(createLabel(QStringLiteral("疲劳检测使用 /dev/video23"), QStringLiteral("secondaryText")));
    m_camera2Preview = new CameraPreviewWidget(QStringLiteral("摄像头 2：/dev/video23"),
                                               QStringLiteral("/dev/video23"),
                                               CameraPreviewWidget::DetectionMode::Fatigue,
                                               fatigueCard);
    fatigueLayout->addWidget(m_camera2Preview, 1);

    layout->addWidget(chipCard, 1);
    layout->addWidget(fatigueCard, 1);
    return area;
}

QWidget *MainWindow::buildSlotResultArea()
{
    QFrame *area = createCard();
    QVBoxLayout *layout = new QVBoxLayout(area);
    layout->setContentsMargins(px(10), px(8), px(10), px(10));
    layout->setSpacing(px(7));

    QHBoxLayout *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(createLabel(QStringLiteral("1 × 4 芯片槽位检测结果"), QStringLiteral("cardTitle")));
    header->addStretch(1);
    QLabel *rule = createLabel(QStringLiteral("判定规则：字符模糊匹配通过 + 无缺陷 = 良品；最多允许缺 3 个字符"),
                               QStringLiteral("ruleText"));
    rule->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(rule);
    layout->addLayout(header);

    QHBoxLayout *slotLayout = new QHBoxLayout;
    slotLayout->setContentsMargins(0, 0, 0, 0);
    slotLayout->setSpacing(px(7));
    for (int i = 0; i < 4; ++i) {
        slotLayout->addWidget(createSlotCard(i + 1));
    }
    layout->addLayout(slotLayout, 1);
    return area;
}

QWidget *MainWindow::buildRightPanel()
{
    QWidget *panel = new QWidget(m_canvas);
    panel->setFixedWidth(px(280));
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(px(8));

    QFrame *templateCard = createCard();
    QVBoxLayout *templateLayout = new QVBoxLayout(templateCard);
    templateLayout->setContentsMargins(px(11), px(10), px(11), px(10));
    templateLayout->setSpacing(px(8));
    templateLayout->addWidget(createLabel(QStringLiteral("芯片模板设置"), QStringLiteral("cardTitle")));

    m_templateEdit = new QLineEdit(templateCard);
    m_templateEdit->setPlaceholderText(QStringLiteral("请输入字符模板，如 STM32F103C8T6"));
    m_defectTypeCombo = new QComboBox(templateCard);
    m_defectTypeCombo->addItem(QStringLiteral("全部缺陷"));
    m_defectTypeCombo->addItem(QStringLiteral("引脚损坏"));
    m_defectTypeCombo->addItem(QStringLiteral("划痕"));

    templateLayout->addWidget(createFieldRow(QStringLiteral("键盘输入字符模板"), m_templateEdit));
    templateLayout->addWidget(createFieldRow(QStringLiteral("缺陷检测类型"), m_defectTypeCombo));
    QPushButton *startInspection = createCommandButton(QStringLiteral("确认并开始检测"), QStringLiteral("primary"));
    connect(startInspection, &QPushButton::clicked, this, &MainWindow::confirmInspectionTemplate);
    templateLayout->addWidget(startInspection);

    connect(m_templateEdit, &QLineEdit::editingFinished, this, [this]() {
        emit templateChanged(m_templateEdit->text().trimmed());
    });
    auto emitDefectOptions = [this]() {
        const QString defectType = m_defectTypeCombo->currentText();
        emit defectOptionsChanged(defectType == QStringLiteral("全部缺陷") ||
                                      defectType == QStringLiteral("引脚损坏"),
                                  defectType == QStringLiteral("全部缺陷") ||
                                      defectType == QStringLiteral("划痕"));
    };
    connect(m_defectTypeCombo, &QComboBox::currentTextChanged, this, emitDefectOptions);

    QFrame *employeeCard = createCard();
    QVBoxLayout *employeeLayout = new QVBoxLayout(employeeCard);
    employeeLayout->setContentsMargins(px(11), px(10), px(11), px(10));
    employeeLayout->setSpacing(px(8));
    QPushButton *openEmployee = createCommandButton(QStringLiteral("员工管理"), QStringLiteral("primary"));
    connect(openEmployee, &QPushButton::clicked, this, &MainWindow::showEmployeeDialog);
    employeeLayout->addWidget(openEmployee);

    layout->addWidget(templateCard);
    layout->addWidget(employeeCard);
    layout->addStretch(1);
    return panel;
}

void MainWindow::showEmployeeDialog()
{
    emit employeeDialogRequested();

    if (m_employeeDialog) {
        m_employeeDialog->raise();
        m_employeeDialog->activateWindow();
        return;
    }

    QDialog *dialog = new QDialog(this);
    m_employeeDialog = dialog;
    dialog->setWindowTitle(QStringLiteral("员工管理"));
    dialog->setObjectName(QStringLiteral("employeeDialog"));
    dialog->resize(px(760), px(460));
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(dialog, &QObject::destroyed, this, [this]() {
        m_employeeDialog = nullptr;
        m_employeePages.clear();
        m_employeeTabs.clear();
        m_leaveRecordGrid = nullptr;
        m_fatigueRecordGrid = nullptr;
    });

    QVBoxLayout *root = new QVBoxLayout(dialog);
    root->setContentsMargins(px(14), px(14), px(14), px(14));
    root->setSpacing(px(10));

    QHBoxLayout *tabs = new QHBoxLayout;
    tabs->setSpacing(px(8));
    const QStringList names = {QStringLiteral("签到签退"), QStringLiteral("工时离岗"), QStringLiteral("疲劳记录")};
    for (int i = 0; i < names.size(); ++i) {
        QPushButton *tab = createCommandButton(names.at(i), i == 0 ? QStringLiteral("tabOn") : QStringLiteral("tab"));
        m_employeeTabs.append(tab);
        tabs->addWidget(tab);
        connect(tab, &QPushButton::clicked, this, [this, i]() {
            setDialogPage(i, m_employeePages, m_employeeTabs);
        });
    }
    tabs->addStretch(1);
    root->addLayout(tabs);

    QWidget *signPage = new QWidget(dialog);
    QGridLayout *signGrid = new QGridLayout(signPage);
    signGrid->setContentsMargins(0, 0, 0, 0);
    signGrid->setSpacing(px(8));
    m_empNameLabel = createLabel(QStringLiteral("--"), QStringLiteral("dialogValue"));
    m_empIdLabel = createLabel(QStringLiteral("--"), QStringLiteral("dialogValue"));
    m_empSignLabel = createLabel(QStringLiteral("--"), QStringLiteral("dialogValue"));
    m_empStationLabel = createLabel(QStringLiteral("--"), QStringLiteral("dialogValue"));
    m_empShiftLabel = createLabel(QStringLiteral("--"), QStringLiteral("dialogValue"));
    signGrid->addWidget(createDialogMetric(QStringLiteral("员工姓名"), QStringLiteral("empName"), signPage), 0, 0);
    signGrid->addWidget(createDialogMetric(QStringLiteral("员工编号"), QStringLiteral("empId"), signPage), 0, 1);
    signGrid->addWidget(createDialogMetric(QStringLiteral("签到状态"), QStringLiteral("empSign"), signPage), 0, 2);
    signGrid->addWidget(createDialogMetric(QStringLiteral("当前工位"), QStringLiteral("empStation"), signPage), 1, 0);
    signGrid->addWidget(createDialogMetric(QStringLiteral("当前班次"), QStringLiteral("empShift"), signPage), 1, 1);
    QFrame *actions = createCard(QStringLiteral("dialogCard"), signPage);
    QGridLayout *actionLayout = new QGridLayout(actions);
    actionLayout->setContentsMargins(px(10), px(10), px(10), px(10));
    actionLayout->setSpacing(px(8));
    QPushButton *signIn = createCommandButton(QStringLiteral("签到"), QStringLiteral("primary"));
    QPushButton *signOut = createCommandButton(QStringLiteral("签退"));
    QPushButton *leave = createCommandButton(QStringLiteral("离岗"), QStringLiteral("warn"));
    QPushButton *back = createCommandButton(QStringLiteral("返岗"));
    connect(signIn, &QPushButton::clicked, this, &MainWindow::employeeSignInRequested);
    connect(signOut, &QPushButton::clicked, this, &MainWindow::employeeSignOutRequested);
    connect(leave, &QPushButton::clicked, this, &MainWindow::employeeLeaveRequested);
    connect(back, &QPushButton::clicked, this, &MainWindow::employeeReturnRequested);
    actionLayout->addWidget(signIn, 0, 0);
    actionLayout->addWidget(signOut, 0, 1);
    actionLayout->addWidget(leave, 1, 0);
    actionLayout->addWidget(back, 1, 1);
    signGrid->addWidget(actions, 1, 2);

    QWidget *leavePage = new QWidget(dialog);
    QVBoxLayout *leaveLayout = new QVBoxLayout(leavePage);
    leaveLayout->setContentsMargins(0, 0, 0, 0);
    leaveLayout->setSpacing(px(8));
    QHBoxLayout *leaveMetrics = new QHBoxLayout;
    leaveMetrics->setSpacing(px(8));
    m_empWorkDurationLabel = createLabel(QStringLiteral("--"), QStringLiteral("dialogValue"));
    m_empLeaveCountLabel = createLabel(QStringLiteral("--"), QStringLiteral("dialogValue"));
    m_empTotalLeaveLabel = createLabel(QStringLiteral("--"), QStringLiteral("dialogValue"));
    m_empLastLeaveLabel = createLabel(QStringLiteral("--"), QStringLiteral("dialogValue"));
    leaveMetrics->addWidget(createDialogMetric(QStringLiteral("工作时长"), QStringLiteral("empWork"), leavePage));
    leaveMetrics->addWidget(createDialogMetric(QStringLiteral("离岗次数"), QStringLiteral("empLeaveCount"), leavePage));
    leaveMetrics->addWidget(createDialogMetric(QStringLiteral("累计离岗"), QStringLiteral("empTotalLeave"), leavePage));
    leaveMetrics->addWidget(createDialogMetric(QStringLiteral("最近离岗"), QStringLiteral("empLastLeave"), leavePage));
    leaveLayout->addLayout(leaveMetrics);
    leaveLayout->addWidget(createRecordTable({QStringLiteral("时间"), QStringLiteral("类型"), QStringLiteral("时长"), QStringLiteral("备注")},
                                             m_leaveRecords,
                                             leavePage,
                                             &m_leaveRecordGrid),
                           1);

    QWidget *fatiguePage = new QWidget(dialog);
    QVBoxLayout *fatigueLayout = new QVBoxLayout(fatiguePage);
    fatigueLayout->setContentsMargins(0, 0, 0, 0);
    fatigueLayout->setSpacing(px(8));
    QHBoxLayout *fatigueMetrics = new QHBoxLayout;
    fatigueMetrics->setSpacing(px(8));
    m_empFatigueRiskLabel = createLabel(QStringLiteral("--"), QStringLiteral("dialogValue"));
    m_empFatigueAlarmLabel = createLabel(QStringLiteral("--"), QStringLiteral("dialogValue"));
    m_empBlinkRateLabel = createLabel(QStringLiteral("--"), QStringLiteral("dialogValue"));
    m_empSuggestedActionLabel = createLabel(QStringLiteral("--"), QStringLiteral("dialogValue"));
    fatigueMetrics->addWidget(createDialogMetric(QStringLiteral("疲劳风险"), QStringLiteral("empFatigueRisk"), fatiguePage));
    fatigueMetrics->addWidget(createDialogMetric(QStringLiteral("疲劳告警"), QStringLiteral("empFatigueAlarm"), fatiguePage));
    fatigueMetrics->addWidget(createDialogMetric(QStringLiteral("眨眼频率"), QStringLiteral("empBlinkRate"), fatiguePage));
    fatigueMetrics->addWidget(createDialogMetric(QStringLiteral("建议动作"), QStringLiteral("empSuggest"), fatiguePage));
    fatigueLayout->addLayout(fatigueMetrics);
    fatigueLayout->addWidget(createRecordTable({QStringLiteral("时间"), QStringLiteral("风险"), QStringLiteral("指标"), QStringLiteral("处理")},
                                               m_fatigueRecords,
                                               fatiguePage,
                                               &m_fatigueRecordGrid),
                             1);

    m_employeePages = {signPage, leavePage, fatiguePage};
    for (QWidget *page : m_employeePages) {
        root->addWidget(page, 1);
    }
    setDialogPage(0, m_employeePages, m_employeeTabs);
    refreshEmployeeDialog();
    applyTheme();
    dialog->show();
}

void MainWindow::confirmInspectionTemplate()
{
    const QString chipTemplate = m_templateEdit ? m_templateEdit->text().trimmed() : QString();
    const QString matchMode = QStringLiteral("模糊匹配（最多缺3字）");
    const double threshold = 0.5;
    const QString defectType = m_defectTypeCombo ? m_defectTypeCombo->currentText() : QString();
    const bool pinMissingEnabled = defectType == QStringLiteral("全部缺陷") ||
        defectType == QStringLiteral("引脚损坏");
    const bool scratchEnabled = defectType == QStringLiteral("全部缺陷") ||
        defectType == QStringLiteral("划痕");
    if (chipTemplate.isEmpty()) {
        if (m_runningStatusLabel) {
            m_runningStatusLabel->setText(QStringLiteral("系统运行状态：模板不能为空"));
        }
        appendAlarmLog(QStringLiteral("模板设置错误"), QStringLiteral("模板为空"));
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_inspectionMutex);
        m_currentTemplate = chipTemplate;
        m_currentMatchMode = matchMode;
        m_currentOcrThreshold = threshold;
        m_inspectionActive = true;
        m_totalCount = 0;
        m_goodCount = 0;
        m_badCount = 0;
        m_nextSlotIndex = 0;
        m_lastMismatchSignature.clear();
    }

    updateKpi(0, 0, 0, 0.0);
    for (int i = 0; i < m_slotWidgets.size(); ++i) {
        updateSlotResult(i + 1,
                         QStringLiteral("未检测"),
                         chipTemplate,
                         QStringLiteral("--"),
                         QStringLiteral("--"),
                         QStringLiteral("--"));
    }
    refreshInspectionStatus();
    appendAlarmLog(QStringLiteral("开始检测"), QStringLiteral("开始检测"));

    emit templateChanged(chipTemplate);
    emit matchModeChanged(matchMode);
    emit ocrThresholdChanged(threshold);
    emit defectOptionsChanged(pinMissingEnabled, scratchEnabled);
    emit startInspectionRequested(chipTemplate,
                                  matchMode,
                                  threshold,
                                  pinMissingEnabled,
                                  scratchEnabled);
}

void MainWindow::startOcrThread()
{
    if (m_ocrRunning.load()) {
        return;
    }
    m_ocrRunning.store(true);
    m_ocrThread = std::thread(&MainWindow::ocrLoop, this);
}

void MainWindow::stopOcrThread()
{
    m_ocrRunning.store(false);
    if (m_ocrThread.joinable()) {
        m_ocrThread.join();
    }
}

void MainWindow::ocrLoop()
{
    HmiOcrDetector detector;
    const bool initialized = detector.init("model/ocr/PP-OCRv5_mobile_det.rknn",
                                           "model/ocr/PP-OCRv5_mobile_rec.rknn");
    QMetaObject::invokeMethod(this, [this, initialized]() {
        setStatusLabel(m_ocrModelLabel, QStringLiteral("字符识别模型"), initialized);
        if (!initialized && m_runningStatusLabel) {
            m_runningStatusLabel->setText(QStringLiteral("系统运行状态：OCR 模型加载失败"));
        }
    }, Qt::QueuedConnection);
    if (!initialized) {
        return;
    }

    while (m_ocrRunning.load()) {
        QString chipTemplate;
        double threshold = 0.5;
        bool active = false;
        {
            std::lock_guard<std::mutex> lock(m_inspectionMutex);
            active = m_inspectionActive;
            chipTemplate = m_currentTemplate;
            threshold = m_currentOcrThreshold;
        }

        if (!active || chipTemplate.isEmpty() || !m_camera1Preview) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        cv::Mat gray;
        if (!m_camera1Preview->latestGrayFrame(gray)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        HmiOcrResult ocr;
        if (detector.recognize(gray, threshold, ocr)) {
            const bool matched = fuzzyChipMatch(chipTemplate.toStdString(), ocr.normalizedText, 3);
            const QString rawText = QString::fromStdString(ocr.rawText);
            const QString normalizedText = QString::fromStdString(ocr.normalizedText);
            QMetaObject::invokeMethod(this, [this, rawText, normalizedText, score = ocr.bestScore, matched]() {
                handleOcrResult(rawText, normalizedText, score, matched);
            }, Qt::QueuedConnection);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(900));
    }
}

void MainWindow::handleOcrResult(const QString &rawText,
                                 const QString &normalizedText,
                                 float bestScore,
                                 bool matched)
{
    const bool hasDefect = m_camera1Preview && m_camera1Preview->hasActiveDetections();
    const QString defectSummary = hasDefect && m_camera1Preview
        ? m_camera1Preview->latestDetectionSummary()
        : QStringLiteral("--");
    const bool finalGood = matched && !hasDefect;

    QString chipTemplate;
    int slotIndex = 0;
    int totalCount = 0;
    int goodCount = 0;
    int badCount = 0;
    bool shouldLogMismatch = false;
    bool shouldLogDefect = false;

    {
        std::lock_guard<std::mutex> lock(m_inspectionMutex);
        if (!m_inspectionActive || m_currentTemplate.isEmpty()) {
            return;
        }

        chipTemplate = m_currentTemplate;
        ++m_totalCount;
        if (finalGood) {
            ++m_goodCount;
        } else {
            ++m_badCount;
        }
        m_nextSlotIndex = (m_nextSlotIndex % std::max(1, m_slotWidgets.size())) + 1;
        slotIndex = m_nextSlotIndex;
        totalCount = m_totalCount;
        goodCount = m_goodCount;
        badCount = m_badCount;

        if (!matched) {
            const QString signature = QStringLiteral("%1|%2").arg(chipTemplate, normalizedText);
            const auto now = std::chrono::steady_clock::now();
            if (signature != m_lastMismatchSignature ||
                now - m_lastMismatchLog >= std::chrono::seconds(5)) {
                m_lastMismatchSignature = signature;
                m_lastMismatchLog = now;
                shouldLogMismatch = true;
            }
        }
        if (hasDefect) {
            const QString signature = QStringLiteral("%1|%2").arg(chipTemplate, defectReasonText(defectSummary));
            const auto now = std::chrono::steady_clock::now();
            if (signature != m_lastDefectSignature ||
                now - m_lastDefectLog >= std::chrono::seconds(5)) {
                m_lastDefectSignature = signature;
                m_lastDefectLog = now;
                shouldLogDefect = true;
            }
        }
    }

    const double goodRate = totalCount > 0
        ? static_cast<double>(goodCount) * 100.0 / static_cast<double>(totalCount)
        : 0.0;
    updateKpi(totalCount, goodCount, badCount, goodRate);

    Q_UNUSED(rawText);
    Q_UNUSED(bestScore);

    const QString defectCategory = hasDefect ? defectReasonText(defectSummary) : QStringLiteral("无缺陷");
    QStringList reasons;
    if (!matched) {
        reasons.append(QStringLiteral("字符不匹配"));
    }
    if (hasDefect) {
        reasons.append(defectCategory);
    }
    updateSlotResult(slotIndex,
                     finalGood ? QStringLiteral("良品") : QStringLiteral("次品"),
                     chipTemplate,
                     matched ? QStringLiteral("匹配") : QStringLiteral("不匹配"),
                     defectCategory,
                     finalGood ? QStringLiteral("--") : reasons.join(QStringLiteral("；")));

    if (shouldLogMismatch) {
        appendAlarmLog(QStringLiteral("字符匹配错误"), QStringLiteral("字符不匹配"));
    }
    if (shouldLogDefect) {
        appendAlarmLog(QStringLiteral("缺陷报警"), defectCategory);
    }
}

void MainWindow::appendAlarmLog(const QString &eventType, const QString &detail)
{
    writeHmiAlarmLog(eventType, detail);
}

void MainWindow::refreshInspectionStatus()
{
    if (m_templateLabel) {
        m_templateLabel->setText(QStringLiteral("当前芯片模板：%1").arg(valueOrDash(m_currentTemplate)));
    }
    if (m_templateVersionLabel) {
        m_templateVersionLabel->setText(QStringLiteral("模板版本：字符模糊匹配 / 最多缺 3 字"));
    }
    if (m_runningStatusLabel) {
        m_runningStatusLabel->setText(m_inspectionActive
            ? QStringLiteral("系统运行状态：检测中")
            : QStringLiteral("系统运行状态：待机"));
    }
}

void MainWindow::applyTheme()
{
    const int base = std::max(9, px(10));
    const QString qss = QStringLiteral(R"(
        QWidget#windowRoot, QWidget#canvas {
            background: %1;
            color: %2;
            font-family: "Microsoft YaHei", "Noto Sans CJK SC", "SimHei", sans-serif;
            font-size: %3px;
        }
        QDialog#employeeDialog {
            background: %1;
            color: %2;
            font-family: "Microsoft YaHei", "Noto Sans CJK SC", "SimHei", sans-serif;
            font-size: %3px;
        }
        QFrame {
            background: %4;
            border: 1px solid %5;
            border-radius: 7px;
        }
        QFrame#header, QFrame#sidebar, QFrame#systemCard, QFrame#dialogCard {
            background: %6;
        }
        QLabel { color: %2; background: transparent; border: none; }
        QLabel#aiIcon {
            color: %1;
            background: %7;
            border: 1px solid %10;
            border-radius: 7px;
            font-weight: 800;
            font-size: %8px;
        }
        QLabel#headerTitle { font-size: %9px; font-weight: 700; color: %2; }
        QLabel#headerTime { font-size: %12px; font-weight: 700; color: %10; }
        QLabel#headerSubtitle, QLabel#secondaryText, QLabel.secondaryText { color: %11; font-size: %3px; }
        QLabel#cardTitle { font-size: %12px; font-weight: 700; color: %2; }
        QLabel#metricTitle, QLabel#miniText, QLabel#ruleText, QLabel#fieldLabel { color: %11; font-size: %13px; }
        QLabel#metricValue { font-size: %14px; font-weight: 800; color: %2; }
        QLabel#dialogValue { font-size: %12px; font-weight: 700; color: %2; }
        QLabel#tableHeader { color: %11; font-weight: 700; background: #0B1622; padding: 5px; }
        QLabel#tableCell { color: %2; background: #0E1A25; padding: 5px; }
        QLabel[state="ok"], QLabel[color="good"] { color: %15; }
        QLabel[state="bad"], QLabel[color="bad"] { color: %16; }
        QLabel[state="warn"], QLabel[color="warn"] { color: %17; }
        QLabel[state="blue"], QLabel[color="blue"] { color: %7; }
        QLabel[state="cyan"], QLabel[color="cyan"] { color: %10; }
        QLabel#statusChip {
            background: #0B1622;
            border: 1px solid %5;
            border-radius: 5px;
            padding: 4px 7px;
        }
        QPushButton {
            color: %2;
            background: #0F1B27;
            border: 1px solid %5;
            border-radius: 6px;
            padding: 6px 8px;
            text-align: center;
        }
        QPushButton:hover { border-color: %7; background: #13283A; }
        QPushButton[selected="true"], QPushButton[kind="primary"], QPushButton[kind="tabOn"] {
            color: white;
            background: %7;
            border-color: %7;
            font-weight: 700;
        }
        QPushButton[warning="true"], QPushButton[kind="warn"] {
            color: %17;
            border-color: %17;
        }
        QPushButton[kind="tab"] {
            color: %11;
            background: #0F1B27;
        }
        QComboBox, QLineEdit {
            color: %2;
            background: #0B1622;
            border: 1px solid %5;
            border-radius: 6px;
            padding: 5px 7px;
            min-height: 24px;
        }
        QFrame[slotState="good"] { border-color: %15; }
        QFrame[slotState="bad"] { border-color: %16; }
        QFrame[slotState="warn"] { border-color: %17; }
    )")
        .arg(kBg)
        .arg(kText)
        .arg(base)
        .arg(kCard)
        .arg(kBorder)
        .arg(kCard2)
        .arg(kBlue)
        .arg(std::max(14, px(16)))
        .arg(std::max(16, px(18)))
        .arg(kCyan)
        .arg(kSubText)
        .arg(std::max(11, px(12)))
        .arg(std::max(9, px(10)))
        .arg(std::max(18, px(22)))
        .arg(kGood)
        .arg(kBad)
        .arg(kWarn);
    qApp->setStyleSheet(qss);
}

void MainWindow::updateScale()
{
    if (!m_canvas) {
        return;
    }

    const QSize available = centralWidget() ? centralWidget()->size() : size();
    const double sx = available.width() / static_cast<double>(DesignWidth);
    const double sy = available.height() / static_cast<double>(DesignHeight);
    m_scale = std::max(0.82, std::min(sx, sy));

    m_canvas->setFixedSize(qRound(DesignWidth * m_scale), qRound(DesignHeight * m_scale));
    if (m_header) {
        m_header->setFixedHeight(px(48));
    }
    if (m_rightPanel) {
        m_rightPanel->setFixedWidth(px(280));
    }
    applyTheme();
}

int MainWindow::px(int value) const
{
    return qRound(value * m_scale);
}

QFrame *MainWindow::createCard(const QString &objectName, QWidget *parent)
{
    QFrame *frame = new QFrame(parent ? parent : m_canvas);
    frame->setFrameShape(QFrame::NoFrame);
    if (!objectName.isEmpty()) {
        frame->setObjectName(objectName);
    }
    return frame;
}

QLabel *MainWindow::createLabel(const QString &text, const QString &objectName, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent ? parent : m_canvas);
    if (!objectName.isEmpty()) {
        label->setObjectName(objectName);
    }
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    return label;
}

QPushButton *MainWindow::createCommandButton(const QString &text, const QString &kind)
{
    QPushButton *button = new QPushButton(text);
    if (!kind.isEmpty()) {
        button->setProperty("kind", kind);
    }
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(px(30));
    return button;
}

QLabel *MainWindow::createStatusChip(const QString &text, const QString &state)
{
    QLabel *label = createLabel(text, QStringLiteral("statusChip"));
    label->setProperty("state", state);
    label->setAlignment(Qt::AlignCenter);
    return label;
}

QWidget *MainWindow::createFieldRow(const QString &label, QWidget *field)
{
    QWidget *row = new QWidget(m_canvas);
    QVBoxLayout *layout = new QVBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(px(4));
    layout->addWidget(createLabel(label, QStringLiteral("fieldLabel"), row));
    layout->addWidget(field);
    return row;
}

QFrame *MainWindow::createMetricCard(const QString &title, const QString &objectName)
{
    QFrame *card = createCard();
    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(px(10), px(8), px(10), px(8));
    layout->setSpacing(px(4));
    QLabel *titleLabel = createLabel(title, QStringLiteral("metricTitle"), card);
    QLabel *value = createLabel(QStringLiteral("--"), QStringLiteral("metricValue"), card);
    if (objectName == QStringLiteral("kpiGood")) {
        value->setProperty("color", "good");
        m_goodKpiLabel = value;
    } else if (objectName == QStringLiteral("kpiBad")) {
        value->setProperty("color", "bad");
        m_badKpiLabel = value;
    } else if (objectName == QStringLiteral("kpiRate")) {
        value->setProperty("color", "blue");
        m_rateKpiLabel = value;
    } else {
        m_totalKpiLabel = value;
    }
    layout->addWidget(titleLabel);
    layout->addWidget(value);
    return card;
}

QFrame *MainWindow::createSlotCard(int slotIndex)
{
    QFrame *card = createCard();
    card->setProperty("slotState", "idle");

    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(px(8), px(7), px(8), px(7));
    layout->setSpacing(px(3));

    SlotWidgets widgets;
    widgets.card = card;
    widgets.slotLabel = createLabel(QStringLiteral("槽位 %1").arg(slotIndex, 2, 10, QLatin1Char('0')),
                                    QStringLiteral("cardTitle"),
                                    card);
    widgets.statusLabel = createLabel(QStringLiteral("状态：未检测"), QStringLiteral("miniText"), card);
    widgets.modelLabel = createLabel(QStringLiteral("芯片型号：--"), QStringLiteral("miniText"), card);
    widgets.textLabel = createLabel(QStringLiteral("字符匹配结果：--"), QStringLiteral("miniText"), card);
    widgets.defectLabel = createLabel(QStringLiteral("缺陷检测结果：--"), QStringLiteral("miniText"), card);
    widgets.reasonLabel = createLabel(QStringLiteral("次品原因：--"), QStringLiteral("miniText"), card);

    for (QLabel *label : {widgets.modelLabel, widgets.textLabel, widgets.defectLabel, widgets.reasonLabel}) {
        label->setWordWrap(true);
        label->setFixedHeight(px(28));
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    layout->addWidget(widgets.slotLabel);
    layout->addWidget(widgets.statusLabel);
    layout->addWidget(widgets.modelLabel);
    layout->addWidget(widgets.textLabel);
    layout->addWidget(widgets.defectLabel);
    layout->addWidget(widgets.reasonLabel);
    layout->addStretch(1);

    m_slotWidgets.append(widgets);
    return card;
}

QFrame *MainWindow::createDialogMetric(const QString &title, const QString &objectName, QWidget *parent)
{
    QFrame *card = createCard(QStringLiteral("dialogCard"), parent);
    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(px(10), px(9), px(10), px(9));
    layout->setSpacing(px(5));
    layout->addWidget(createLabel(title, QStringLiteral("metricTitle"), card));

    QLabel *value = nullptr;
    if (objectName == QStringLiteral("empName")) value = m_empNameLabel;
    else if (objectName == QStringLiteral("empId")) value = m_empIdLabel;
    else if (objectName == QStringLiteral("empSign")) value = m_empSignLabel;
    else if (objectName == QStringLiteral("empStation")) value = m_empStationLabel;
    else if (objectName == QStringLiteral("empShift")) value = m_empShiftLabel;
    else if (objectName == QStringLiteral("empWork")) value = m_empWorkDurationLabel;
    else if (objectName == QStringLiteral("empLeaveCount")) value = m_empLeaveCountLabel;
    else if (objectName == QStringLiteral("empTotalLeave")) value = m_empTotalLeaveLabel;
    else if (objectName == QStringLiteral("empLastLeave")) value = m_empLastLeaveLabel;
    else if (objectName == QStringLiteral("empFatigueRisk")) value = m_empFatigueRiskLabel;
    else if (objectName == QStringLiteral("empFatigueAlarm")) value = m_empFatigueAlarmLabel;
    else if (objectName == QStringLiteral("empBlinkRate")) value = m_empBlinkRateLabel;
    else if (objectName == QStringLiteral("empSuggest")) value = m_empSuggestedActionLabel;

    if (value) {
        value->setParent(card);
        layout->addWidget(value);
    }
    return card;
}

QWidget *MainWindow::createRecordTable(const QStringList &headers,
                                       const QVector<QStringList> &rows,
                                       QWidget *parent,
                                       QGridLayout **gridOut)
{
    QFrame *table = createCard(QStringLiteral("dialogCard"), parent);
    QGridLayout *grid = new QGridLayout(table);
    grid->setContentsMargins(px(8), px(8), px(8), px(8));
    grid->setSpacing(1);
    updateRecordTable(grid, headers, rows);
    if (gridOut) {
        *gridOut = grid;
    }
    return table;
}

void MainWindow::updateRecordTable(QGridLayout *grid, const QStringList &headers, const QVector<QStringList> &rows)
{
    if (!grid) {
        return;
    }

    while (QLayoutItem *item = grid->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    for (int col = 0; col < headers.size(); ++col) {
        QLabel *label = createLabel(headers.at(col), QStringLiteral("tableHeader"), grid->parentWidget());
        label->setAlignment(Qt::AlignCenter);
        grid->addWidget(label, 0, col);
    }

    const int rowCount = std::max(1, rows.size());
    for (int row = 0; row < rowCount; ++row) {
        const QStringList values = row < rows.size() ? rows.at(row) : QStringList{QStringLiteral("等待数据")};
        for (int col = 0; col < headers.size(); ++col) {
            QLabel *cell = createLabel(col < values.size() ? values.at(col) : QStringLiteral("--"),
                                      QStringLiteral("tableCell"),
                                      grid->parentWidget());
            cell->setAlignment(Qt::AlignCenter);
            grid->addWidget(cell, row + 1, col);
        }
    }
}

void MainWindow::setDialogPage(int index, const QVector<QWidget *> &pages, const QVector<QPushButton *> &tabs)
{
    for (int i = 0; i < pages.size(); ++i) {
        pages.at(i)->setVisible(i == index);
    }
    for (int i = 0; i < tabs.size(); ++i) {
        tabs.at(i)->setProperty("kind", i == index ? "tabOn" : "tab");
        polish(tabs.at(i));
    }
}

void MainWindow::setTemplateOptions(const QStringList &templates)
{
    if (!m_templateEdit) {
        return;
    }
    const QSignalBlocker blocker(m_templateEdit);
    m_templateEdit->setText(templates.isEmpty() ? QString() : templates.first());
}

void MainWindow::setMatchModeOptions(const QStringList &modes)
{
    if (!m_matchModeCombo) {
        return;
    }
    const QSignalBlocker blocker(m_matchModeCombo);
    m_matchModeCombo->clear();
    if (modes.isEmpty()) {
        m_matchModeCombo->addItem(QStringLiteral("模糊匹配（最多缺3字）"));
    } else {
        m_matchModeCombo->addItems(modes);
    }
}

void MainWindow::updateDeviceStatus(const DeviceStatus &status)
{
    if (m_plcStatusLabel) {
        m_plcStatusLabel->setText(status.plcConnected ? QStringLiteral("PLC：已连接") : QStringLiteral("PLC：未连接"));
        m_plcStatusLabel->setProperty("state", status.plcConnected ? "ok" : "bad");
        polish(m_plcStatusLabel);
    }
    if (m_camera1StatusLabel) {
        m_camera1StatusLabel->setText(status.camera1Online ? QStringLiteral("相机 1：在线") : QStringLiteral("相机 1：离线"));
        m_camera1StatusLabel->setProperty("state", status.camera1Online ? "cyan" : "bad");
        polish(m_camera1StatusLabel);
    }
    if (m_camera2StatusLabel) {
        m_camera2StatusLabel->setText(status.camera2Online ? QStringLiteral("相机 2：在线") : QStringLiteral("相机 2：离线"));
        m_camera2StatusLabel->setProperty("state", status.camera2Online ? "cyan" : "bad");
        polish(m_camera2StatusLabel);
    }
    if (m_shiftLabel) {
        m_shiftLabel->setText(QStringLiteral("班次：%1").arg(valueOrDash(status.shiftName)));
        m_shiftLabel->setProperty("state", status.shiftName.isEmpty() ? "warn" : "blue");
        polish(m_shiftLabel);
    }
    if (m_camera1Preview) {
        m_camera1Preview->setOnline(status.camera1Online);
    }
    if (m_camera2Preview) {
        m_camera2Preview->setOnline(status.camera2Online);
    }
}

void MainWindow::updateSystemStatus(const SystemStatus &status)
{
    setStatusLabel(m_visionModelLabel, QStringLiteral("视觉检测模型"), status.visionModelLoaded);
    setStatusLabel(m_ocrModelLabel, QStringLiteral("字符识别模型"), status.ocrModelLoaded);
    if (m_templateLabel) {
        m_templateLabel->setText(QStringLiteral("当前芯片模板：%1").arg(valueOrDash(status.chipTemplate)));
    }
    if (m_templateVersionLabel) {
        m_templateVersionLabel->setText(QStringLiteral("模板版本：%1").arg(valueOrDash(status.templateVersion)));
    }
    if (m_runningStatusLabel) {
        m_runningStatusLabel->setText(QStringLiteral("系统运行状态：%1").arg(valueOrDash(status.runningStatus)));
    }
}

void MainWindow::updateKpiData(const KpiData &data)
{
    updateKpi(data.totalCount, data.goodCount, data.badCount, data.goodRate);
}

void MainWindow::updateKpi(int totalCount, int goodCount, int badCount, double goodRate)
{
    m_totalKpiLabel->setText(metricText(totalCount));
    m_goodKpiLabel->setText(metricText(goodCount));
    m_badKpiLabel->setText(metricText(badCount));
    m_rateKpiLabel->setText(QStringLiteral("%1%").arg(goodRate, 0, 'f', 1));
}

void MainWindow::updateSlotResult(const SlotResult &result)
{
    const int index = result.slotIndex - 1;
    if (index < 0 || index >= m_slotWidgets.size()) {
        return;
    }

    SlotWidgets &slot = m_slotWidgets[index];
    const QString state = slotState(result.status);
    slot.card->setProperty("slotState", state);
    slot.statusLabel->setText(QStringLiteral("状态：%1").arg(valueOrDash(result.status)));
    slot.statusLabel->setProperty("color", state);
    slot.modelLabel->setText(QStringLiteral("芯片型号：%1").arg(wrapSlotValue(valueOrDash(result.chipModel))));
    slot.textLabel->setText(QStringLiteral("字符匹配结果：%1").arg(wrapSlotValue(valueOrDash(result.textResult))));
    slot.defectLabel->setText(QStringLiteral("缺陷检测结果：%1").arg(wrapSlotValue(valueOrDash(result.defectResult))));
    slot.reasonLabel->setText(QStringLiteral("次品原因：%1").arg(wrapSlotValue(valueOrDash(result.reason))));
    polish(slot.card);
    polish(slot.statusLabel);
}

void MainWindow::updateSlotResult(int slotIndex,
                                  const QString &status,
                                  const QString &chipModel,
                                  const QString &textResult,
                                  const QString &defectResult,
                                  const QString &reason)
{
    SlotResult result;
    result.slotIndex = slotIndex;
    result.status = status;
    result.chipModel = chipModel;
    result.textResult = textResult;
    result.defectResult = defectResult;
    result.reason = reason;
    updateSlotResult(result);
}

void MainWindow::updateAllSlotResults(const QVector<SlotResult> &results)
{
    for (const SlotResult &result : results) {
        updateSlotResult(result);
    }
}

void MainWindow::updateEmployeeStatus(const EmployeeStatus &status)
{
    m_employeeStatus = status;
    if (m_camera2Preview) {
        m_camera2Preview->setFatigueText(QStringLiteral("疲劳风险：%1").arg(valueOrDash(status.fatigueRisk)));
    }
    refreshEmployeeDialog();
}

void MainWindow::updateLeaveRecords(const QVector<QStringList> &records)
{
    m_leaveRecords = records;
    if (m_leaveRecordGrid) {
        updateRecordTable(m_leaveRecordGrid,
                          {QStringLiteral("时间"), QStringLiteral("类型"), QStringLiteral("时长"), QStringLiteral("备注")},
                          m_leaveRecords);
    }
}

void MainWindow::updateFatigueRecords(const QVector<QStringList> &records)
{
    m_fatigueRecords = records;
    if (m_fatigueRecordGrid) {
        updateRecordTable(m_fatigueRecordGrid,
                          {QStringLiteral("时间"), QStringLiteral("风险"), QStringLiteral("指标"), QStringLiteral("处理")},
                          m_fatigueRecords);
    }
}

void MainWindow::updateCameraFrame(int cameraId, const QImage &frame)
{
    if (cameraId == 1 || cameraId == 21) {
        m_camera1Preview->setFrame(frame);
    } else if (cameraId == 2 || cameraId == 23) {
        m_camera2Preview->setFrame(frame);
    }
}

void MainWindow::refreshEmployeeDialog()
{
    if (!m_employeeDialog) {
        return;
    }

    if (m_empNameLabel) m_empNameLabel->setText(valueOrDash(m_employeeStatus.name));
    if (m_empIdLabel) m_empIdLabel->setText(valueOrDash(m_employeeStatus.employeeId));
    if (m_empSignLabel) m_empSignLabel->setText(valueOrDash(m_employeeStatus.signStatus));
    if (m_empStationLabel) m_empStationLabel->setText(valueOrDash(m_employeeStatus.station));
    if (m_empShiftLabel) m_empShiftLabel->setText(valueOrDash(m_employeeStatus.shiftName));
    if (m_empWorkDurationLabel) m_empWorkDurationLabel->setText(valueOrDash(m_employeeStatus.workDuration));
    if (m_empLeaveCountLabel) {
        m_empLeaveCountLabel->setText(m_employeeStatus.leaveCount < 0 ? QStringLiteral("--") : QString::number(m_employeeStatus.leaveCount));
    }
    if (m_empTotalLeaveLabel) m_empTotalLeaveLabel->setText(valueOrDash(m_employeeStatus.totalLeaveDuration));
    if (m_empLastLeaveLabel) m_empLastLeaveLabel->setText(valueOrDash(m_employeeStatus.lastLeaveTime));
    if (m_empFatigueRiskLabel) m_empFatigueRiskLabel->setText(valueOrDash(m_employeeStatus.fatigueRisk));
    if (m_empFatigueAlarmLabel) {
        m_empFatigueAlarmLabel->setText(m_employeeStatus.fatigueAlarmCount < 0 ? QStringLiteral("--") : QString::number(m_employeeStatus.fatigueAlarmCount));
    }
    if (m_empBlinkRateLabel) m_empBlinkRateLabel->setText(valueOrDash(m_employeeStatus.blinkRate));
    if (m_empSuggestedActionLabel) m_empSuggestedActionLabel->setText(valueOrDash(m_employeeStatus.suggestedAction));
}

void MainWindow::setStatusLabel(QLabel *label, const QString &prefix, bool ok)
{
    if (!label) {
        return;
    }
    label->setText(QStringLiteral("%1：%2").arg(prefix, ok ? QStringLiteral("已加载") : QStringLiteral("--")));
    label->setProperty("state", ok ? "ok" : "warn");
    polish(label);
}

void MainWindow::polish(QWidget *widget)
{
    if (!widget) {
        return;
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QString MainWindow::valueOrDash(const QString &value) const
{
    return value.trimmed().isEmpty() ? QStringLiteral("--") : value;
}

QString MainWindow::slotState(const QString &status) const
{
    if (status == QStringLiteral("良品")) {
        return QStringLiteral("good");
    }
    if (status == QStringLiteral("次品")) {
        return QStringLiteral("bad");
    }
    if (status == QStringLiteral("复检")) {
        return QStringLiteral("warn");
    }
    return QStringLiteral("warn");
}

QColor MainWindow::stateColor(const QString &state) const
{
    if (state == QStringLiteral("good") || state == QStringLiteral("ok")) return QColor(kGood);
    if (state == QStringLiteral("bad")) return QColor(kBad);
    if (state == QStringLiteral("blue")) return QColor(kBlue);
    if (state == QStringLiteral("cyan")) return QColor(kCyan);
    return QColor(kWarn);
}
