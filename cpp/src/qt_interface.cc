#include "qt_interface.h"
#include "logger.h"

// ============================================================
// 单例实现
// ============================================================
QtInterface& QtInterface::GetInstance() {
    static QtInterface instance;
    return instance;
}

// ============================================================
// 构造函数和析构函数
// ============================================================
QtInterface::QtInterface()
    : initialized_(false),
      running_(false) {
}

QtInterface::~QtInterface() {
    Quit();
}

// ============================================================
// 初始化 QT 界面
// ============================================================
bool QtInterface::Init(int argc, char* argv[]) {
    if (initialized_) {
        return true;
    }

    // TODO: 初始化 QT 应用
    // QApplication app(argc, argv);

    initialized_ = true;
    LOG_INFO("Qt interface initialized");
    return true;
}

// ============================================================
// 运行主循环
// ============================================================
int QtInterface::Run() {
    if (!initialized_) {
        LOG_ERROR("Qt interface not initialized");
        return -1;
    }

    running_ = true;

    // TODO: 运行 QT 主循环
    // return app.exec();

    LOG_INFO("Qt interface running");
    return 0;
}

// ============================================================
// 退出
// ============================================================
void QtInterface::Quit() {
    running_ = false;
    LOG_INFO("Qt interface quit");
}

// ============================================================
// 注册回调函数
// ============================================================
void QtInterface::RegisterOcrCallback(std::function<void(const OcrResult&)> callback) {
    ocr_callback_ = callback;
}

void QtInterface::RegisterFatigueCallback(std::function<void(const FatigueResult&)> callback) {
    fatigue_callback_ = callback;
}

void QtInterface::RegisterDefectCallback(std::function<void(const DefectResult&)> callback) {
    defect_callback_ = callback;
}

void QtInterface::RegisterStatusCallback(std::function<void(const std::string&)> callback) {
    status_callback_ = callback;
}

// ============================================================
// 显示图像
// ============================================================
void QtInterface::ShowImage(const uint8_t* data, int width, int height, int channels) {
    if (!running_) return;

    // TODO: 在 QT 界面显示图像
    // 可以使用 QLabel + QPixmap 或 QOpenGLWidget
}

// ============================================================
// 显示状态信息
// ============================================================
void QtInterface::ShowStatus(const std::string& status) {
    if (!running_) return;

    // TODO: 在状态栏显示信息

    if (status_callback_) {
        status_callback_(status);
    }
}

// ============================================================
// 显示检测结果
// ============================================================
void QtInterface::ShowOcrResult(const OcrResult& result) {
    if (!running_) return;

    // TODO: 在界面上显示 OCR 结果

    if (ocr_callback_) {
        ocr_callback_(result);
    }
}

void QtInterface::ShowFatigueResult(const FatigueResult& result) {
    if (!running_) return;

    // TODO: 在界面上显示疲劳检测结果

    if (fatigue_callback_) {
        fatigue_callback_(result);
    }
}

void QtInterface::ShowDefectResult(const DefectResult& result) {
    if (!running_) return;

    // TODO: 在界面上显示缺陷检测结果

    if (defect_callback_) {
        defect_callback_(result);
    }
}
