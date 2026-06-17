#ifndef HMI_MAINWINDOW_H
#define HMI_MAINWINDOW_H

#include "serial_port.h"
#include "cloud_uploader.h"

#include <QImage>
#include <QMainWindow>
#include <QRectF>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include <opencv2/core.hpp>

class QComboBox;
class QDialog;
class QFrame;
class QGridLayout;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QTimer;
class QVBoxLayout;
class QWidget;

class HmiOcrDetector;

struct HmiRuntimeOptions {
    QString serialPort = QStringLiteral("/dev/ttyS9");
    int serialBaudrate = 115200;
    QString motorSerialPort = QStringLiteral("/dev/ttyS8");
    int motorSerialBaudrate = 115200;
    QString arrivalCommand = QStringLiteral("DONE");
    QString moveCommand = QStringLiteral("hex:AA 55 20 FF");
    QString employeeDatabasePath = QStringLiteral("employee_host.db");
    QString cloudUploadConfigPath = QStringLiteral("config/cloud_upload.ini");
    int chipSlots = 4;
};

struct DeviceStatus {
    bool plcConnected = false;
    bool camera1Online = false;
    bool camera2Online = false;
    QString shiftName;
};

struct KpiData {
    int totalCount = 0;
    int goodCount = 0;
    int badCount = 0;
    double goodRate = 0.0;
};

struct SlotResult {
    int slotIndex = 0;
    QString status;
    QString chipModel;
    QString textResult;
    QString defectResult;
    QString reason;
};

struct SystemStatus {
    bool visionModelLoaded = false;
    bool ocrModelLoaded = false;
    QString chipTemplate;
    QString templateVersion;
    QString runningStatus;
};

struct EmployeeStatus {
    QString name;
    QString employeeId;
    QString signStatus;
    QString station;
    QString shiftName;
    QString workDuration;
    int leaveCount = -1;
    QString totalLeaveDuration;
    QString lastLeaveTime;
    QString fatigueRisk;
    int fatigueAlarmCount = -1;
    QString blinkRate;
    QString suggestedAction;
};

struct HmiEmployeeItem {
    uint32_t id = 0;
    QString name;
};

struct HmiAttendanceRecord {
    int recordId = 0;
    uint32_t employeeId = 0;
    QString name;
    qint64 checkinTime = 0;
    qint64 checkoutTime = 0;
    qint64 durationSec = 0;
};

class CameraPreviewWidget : public QWidget {
    Q_OBJECT

public:
    enum class DetectionMode {
        None,
        Defect,
        Fatigue
    };

    explicit CameraPreviewWidget(const QString &cameraName,
                                 const QString &devicePath,
                                 DetectionMode detectionMode,
                                 QWidget *parent = nullptr);
    ~CameraPreviewWidget() override;

    bool startCamera(int width = 640, int height = 480);
    void stopCamera();
    void setFrame(const QImage &frame);
    void setOnline(bool online);
    void setMessage(const QString &message);
    void setFatigueText(const QString &text);
    void setFatigueAlarmCallback(std::function<void(const QString&, int)> callback);
    bool latestGrayFrame(cv::Mat &gray) const;
    bool hasActiveDetections() const;
    QString latestDetectionSummary() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRect imageRect(const QRect &targetRect, const QSize &imageSize) const;
    void readFrame();
    void drawPlaceholder(QPainter &painter, const QRect &area);
    void drawFatigueOverlay(QPainter &painter, const QRect &area);
    void drawDetectionOverlay(QPainter &painter, const QRect &imageArea);
    void updateInferenceFrame(const cv::Mat &rgb);
    void startDetection();
    void stopDetection();
    void detectionLoop();

private:
    struct DetectionOverlay {
        QRectF modelBox;
        QString label;
        float score = 0.0f;
    };

    struct FatigueSample {
        std::chrono::steady_clock::time_point time;
        bool closed = false;
    };

    enum class FatigueLevel {
        Awake,
        Fatigue,
        Severe
    };

    void updateFatigueDecision(const QVector<DetectionOverlay> &overlays,
                               const std::chrono::steady_clock::time_point &now);
    void setFatigueDecision(FatigueLevel level,
                            const QString &triggerText,
                            int fatigueScore);
    void resetFatigueWarning();
    bool isCenterVisionDetection(const QRectF &modelBox) const;
    bool hasDetectionLabel(const QVector<DetectionOverlay> &overlays, const QString &label) const;
    QString fatigueLevelText(FatigueLevel level) const;

    QString m_cameraName;
    QString m_devicePath;
    QString m_message;
    QString m_fatigueText;
    QImage m_frame;
    QTimer *m_frameTimer;
    void *m_capture;
    int m_captureServicePid = -1;
    QString m_captureFramePath;
    bool m_online;
    bool m_fatigueOverlay;
    DetectionMode m_detectionMode;
    std::atomic<bool> m_detectionRunning;
    std::thread m_detectionThread;
    mutable std::mutex m_inferenceMutex;
    cv::Mat m_inferenceGray;
    uint64_t m_inferenceSeq = 0;
    mutable std::mutex m_overlayMutex;
    QVector<DetectionOverlay> m_detectionOverlays;
    bool m_detectionReady = false;
    std::mutex m_fatigueMutex;
    FatigueLevel m_fatigueLevel = FatigueLevel::Awake;
    int m_fatigueScore = 0;
    std::deque<FatigueSample> m_fatigueSamples;
    std::deque<std::chrono::steady_clock::time_point> m_closedEyeEvents;
    std::deque<std::chrono::steady_clock::time_point> m_yawnEvents;
    std::function<void(const QString&, int)> m_fatigueAlarmCallback;
    bool m_currentClosed = false;
    bool m_currentMouthOpen = false;
    bool m_currentFatigue = false;
    bool m_currentClosedEventCounted = false;
    bool m_currentMouthOpenCounted = false;
    std::chrono::steady_clock::time_point m_closedStart;
    std::chrono::steady_clock::time_point m_mouthOpenStart;
    std::chrono::steady_clock::time_point m_fatigueStart;
    std::chrono::steady_clock::time_point m_warningStart;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const HmiRuntimeOptions &options = HmiRuntimeOptions(), QWidget *parent = nullptr);
    ~MainWindow() override;

    void setTemplateOptions(const QStringList &templates);
    void setMatchModeOptions(const QStringList &modes);

public slots:
    void updateDeviceStatus(const DeviceStatus &status);
    void updateSystemStatus(const SystemStatus &status);
    void updateKpiData(const KpiData &data);
    void updateKpi(int totalCount, int goodCount, int badCount, double goodRate);
    void updateSlotResult(const SlotResult &result);
    void updateSlotResult(int slotIndex,
                          const QString &status,
                          const QString &chipModel,
                          const QString &textResult,
                          const QString &defectResult,
                          const QString &reason);
    void updateAllSlotResults(const QVector<SlotResult> &results);
    void updateEmployeeStatus(const EmployeeStatus &status);
    void updateLeaveRecords(const QVector<QStringList> &records);
    void updateFatigueRecords(const QVector<QStringList> &records);
    void updateCameraFrame(int cameraId, const QImage &frame);

signals:
    void templateChanged(const QString &chipTemplate);
    void matchModeChanged(const QString &matchMode);
    void ocrThresholdChanged(double threshold);
    void defectOptionsChanged(bool pinMissingEnabled, bool scratchEnabled);
    void startInspectionRequested(const QString &chipTemplate,
                                  const QString &matchMode,
                                  double ocrThreshold,
                                  bool pinMissingEnabled,
                                  bool scratchEnabled);
    void employeeDialogRequested();
    void employeeSignInRequested();
    void employeeSignOutRequested();
    void employeeLeaveRequested();
    void employeeReturnRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void refreshClock();
    void showEmployeeDialog();
    void confirmInspectionTemplate();
    void requestMotorForward();
    void requestMotorBack();
    void requestMotorAutoRun();
    void requestEmployeeEnroll();
    void requestEmployeeCheckin();
    void requestEmployeeCheckout();
    void requestEmployeeDelete();
    void onEmployeeOperationTimeout();

private:
    QWidget *buildHeader();
    QWidget *buildKpiCards();
    QWidget *buildCameraArea();
    QWidget *buildSlotResultArea();
    QWidget *buildRightPanel();
    void applyTheme();
    void updateScale();

    void buildUi();
    int px(int value) const;
    QFrame *createCard(const QString &objectName = QString(), QWidget *parent = nullptr);
    QLabel *createLabel(const QString &text, const QString &objectName = QString(), QWidget *parent = nullptr);
    QPushButton *createCommandButton(const QString &text, const QString &kind = QString());
    QLabel *createStatusChip(const QString &text, const QString &state);
    QWidget *createFieldRow(const QString &label, QWidget *field);
    QFrame *createMetricCard(const QString &title, const QString &objectName);
    QFrame *createSlotCard(int slotIndex);
    QFrame *createDialogMetric(const QString &title, const QString &objectName, QWidget *parent);
    QWidget *createRecordTable(const QStringList &headers,
                               const QVector<QStringList> &rows,
                               QWidget *parent,
                               QGridLayout **gridOut = nullptr);
    void updateRecordTable(QGridLayout *grid, const QStringList &headers, const QVector<QStringList> &rows);
    void setDialogPage(int index, const QVector<QWidget *> &pages, const QVector<QPushButton *> &tabs);
    void refreshEmployeeDialog();
    void refreshEmployeeRecords();
    bool openEmployeeDatabase();
    bool initializeEmployeeDatabase();
    QString employeeDatabaseError() const;
    bool addEmployeeToDatabase(uint32_t employeeId, const QString &name, qint64 createdAt);
    bool employeeNameFromDatabase(uint32_t employeeId, QString &name) const;
    QVector<HmiEmployeeItem> employeesFromDatabase() const;
    QVector<HmiEmployeeItem> employeesOnDuty() const;
    bool deleteEmployeeFromDatabase(uint32_t employeeId);
    bool addEmployeeCheckin(uint32_t employeeId, qint64 checkinTime);
    bool finishEmployeeCheckout(uint32_t employeeId, qint64 checkoutTime, qint64 durationSec);
    QVector<HmiAttendanceRecord> employeeAttendanceRecords() const;
    bool sendEmployeeFrame(uint8_t command, const QByteArray &payload);
    void handleEmployeeFrame(uint8_t command, const QByteArray &payload);
    void setEmployeeWaiting(bool waiting, const QString &text);
    void completeEmployeeOperation(const QString &text);
    void failEmployeeOperation(const QString &title, const QString &text);
    void refreshEmployeeCheckoutList();
    void refreshEmployeeDeleteList();
    void setStatusLabel(QLabel *label, const QString &prefix, bool ok);
    void polish(QWidget *widget);
    QString valueOrDash(const QString &value) const;
    QString slotState(const QString &status) const;
    QColor stateColor(const QString &state) const;
    void startOcrThread();
    void stopOcrThread();
    void ocrLoop();
    void startSerialThread();
    void stopSerialThread();
    void serialLoop();
    void motorSerialLoop();
    bool consumeArrivalRequest();
    bool sendMotorCommand(uint8_t command, const QString &label);
    void sendMoveCommand();
    void setSerialStatus(bool online, const QString &detail);
    void handleOcrResult(const QString &rawText,
                         const QString &normalizedText,
                         float bestScore,
                         bool matched);
    void appendAlarmLog(const QString &eventType, const QString &detail);
    void refreshInspectionStatus();

private:
    struct SlotWidgets {
        QFrame *card = nullptr;
        QLabel *slotLabel = nullptr;
        QLabel *statusLabel = nullptr;
        QLabel *modelLabel = nullptr;
        QLabel *textLabel = nullptr;
        QLabel *defectLabel = nullptr;
        QLabel *reasonLabel = nullptr;
    };

    static constexpr int DesignWidth = 1024;
    static constexpr int DesignHeight = 576;

    QWidget *m_canvas = nullptr;
    QWidget *m_header = nullptr;
    QWidget *m_rightPanel = nullptr;
    QLabel *m_plcStatusLabel = nullptr;
    QLabel *m_camera1StatusLabel = nullptr;
    QLabel *m_camera2StatusLabel = nullptr;
    QLabel *m_shiftLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_visionModelLabel = nullptr;
    QLabel *m_ocrModelLabel = nullptr;
    QLabel *m_templateLabel = nullptr;
    QLabel *m_templateVersionLabel = nullptr;
    QLabel *m_runningStatusLabel = nullptr;
    QLabel *m_totalKpiLabel = nullptr;
    QLabel *m_goodKpiLabel = nullptr;
    QLabel *m_badKpiLabel = nullptr;
    QLabel *m_rateKpiLabel = nullptr;
    CameraPreviewWidget *m_camera1Preview = nullptr;
    CameraPreviewWidget *m_camera2Preview = nullptr;
    QLineEdit *m_templateEdit = nullptr;
    QComboBox *m_matchModeCombo = nullptr;
    QComboBox *m_defectTypeCombo = nullptr;
    QLineEdit *m_ocrThresholdEdit = nullptr;
    QVector<SlotWidgets> m_slotWidgets;
    QTimer *m_clockTimer = nullptr;
    double m_scale = 1.0;
    std::atomic<bool> m_ocrRunning{false};
    std::thread m_ocrThread;
    HmiRuntimeOptions m_options;
    SerialPort m_serial;
    std::atomic<bool> m_serialRunning{false};
    std::atomic<bool> m_serialOnline{false};
    SerialPort m_motorSerial;
    std::atomic<bool> m_motorSerialRunning{false};
    std::atomic<bool> m_motorSerialOnline{false};
    std::atomic<uint64_t> m_arrivalRequests{0};
    std::atomic<int> m_pendingMotorCommand{0};
    std::atomic<qint64> m_pendingMotorCommandMs{0};
    std::atomic<int> m_completedForwardMoves{0};
    std::thread m_serialThread;
    std::thread m_motorSerialThread;
    CloudUploader m_cloudUploader;
    std::mutex m_inspectionMutex;
    bool m_inspectionActive = false;
    QString m_currentTemplate;
    QString m_currentMatchMode;
    double m_currentOcrThreshold = 0.5;
    int m_totalCount = 0;
    int m_goodCount = 0;
    int m_badCount = 0;
    int m_nextSlotIndex = 0;
    QString m_lastMismatchSignature;
    std::chrono::steady_clock::time_point m_lastMismatchLog;
    QString m_lastDefectSignature;
    std::chrono::steady_clock::time_point m_lastDefectLog;

    QDialog *m_employeeDialog = nullptr;
    QVector<QWidget *> m_employeePages;
    QVector<QPushButton *> m_employeeTabs;
    QLineEdit *m_employeeIdEdit = nullptr;
    QLineEdit *m_employeeNameEdit = nullptr;
    QPushButton *m_employeeEnrollButton = nullptr;
    QPushButton *m_employeeCheckinButton = nullptr;
    QPushButton *m_employeeCheckoutButton = nullptr;
    QPushButton *m_employeeDeleteButton = nullptr;
    QComboBox *m_employeeCheckoutCombo = nullptr;
    QComboBox *m_employeeDeleteCombo = nullptr;
    QLabel *m_employeeEnrollStatusLabel = nullptr;
    QLabel *m_employeeCheckinStatusLabel = nullptr;
    QLabel *m_employeeCheckoutStatusLabel = nullptr;
    QLabel *m_employeeDeleteStatusLabel = nullptr;
    QLabel *m_empNameLabel = nullptr;
    QLabel *m_empIdLabel = nullptr;
    QLabel *m_empSignLabel = nullptr;
    QLabel *m_empStationLabel = nullptr;
    QLabel *m_empShiftLabel = nullptr;
    QLabel *m_empWorkDurationLabel = nullptr;
    QLabel *m_empLeaveCountLabel = nullptr;
    QLabel *m_empTotalLeaveLabel = nullptr;
    QLabel *m_empLastLeaveLabel = nullptr;
    QLabel *m_empFatigueRiskLabel = nullptr;
    QLabel *m_empFatigueAlarmLabel = nullptr;
    QLabel *m_empBlinkRateLabel = nullptr;
    QLabel *m_empSuggestedActionLabel = nullptr;
    QGridLayout *m_leaveRecordGrid = nullptr;
    QGridLayout *m_fatigueRecordGrid = nullptr;
    QGridLayout *m_employeeRecordGrid = nullptr;
    QTimer *m_employeeOperationTimer = nullptr;

    enum class EmployeeOperation {
        None,
        Enroll,
        Checkin,
        Checkout
    };
    EmployeeOperation m_employeeOperation = EmployeeOperation::None;
    uint32_t m_pendingEmployeeId = 0;
    QString m_pendingEmployeeName;
    QSqlDatabase m_employeeDb;
    QString m_employeeDbConnectionName;
    QString m_employeeDbLastError;

    EmployeeStatus m_employeeStatus;
    QVector<QStringList> m_leaveRecords;
    QVector<QStringList> m_fatigueRecords;
    QVector<QStringList> m_employeeRecords;
};

#endif // HMI_MAINWINDOW_H
