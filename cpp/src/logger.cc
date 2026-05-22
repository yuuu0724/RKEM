#include "logger.h"
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

// ============================================================
// 单例实现
// ============================================================
Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

// ============================================================
// 构造函数和析构函数
// ============================================================
Logger::Logger()
    : running_(false),
      current_level_(LOG_INFO) {
}

Logger::~Logger() {
    Shutdown();
}

// ============================================================
// 初始化日志系统
// ============================================================
bool Logger::Init(const std::string& log_dir) {
    std::lock_guard<std::mutex> lock(log_mutex_);

    log_dir_ = log_dir;

    // 创建日志目录
    mkdir(log_dir_.c_str(), 0755);

    // 打开日志文件
    ocr_log_file_.open(log_dir_ + "/ocr_results.log", std::ios::app);
    fatigue_log_file_.open(log_dir_ + "/fatigue_results.log", std::ios::app);
    defect_log_file_.open(log_dir_ + "/defect_results.log", std::ios::app);
    debug_log_file_.open(log_dir_ + "/debug.log", std::ios::app);

    if (!ocr_log_file_.is_open() || !fatigue_log_file_.is_open() ||
        !defect_log_file_.is_open() || !debug_log_file_.is_open()) {
        return false;
    }

    // 启动日志写入线程
    running_ = true;
    writer_thread_ = std::thread(&Logger::LogWriterThread, this);

    return true;
}

// ============================================================
// 关闭日志系统
// ============================================================
void Logger::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        running_ = false;
    }

    log_cv_.notify_all();

    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }

    if (ocr_log_file_.is_open()) ocr_log_file_.close();
    if (fatigue_log_file_.is_open()) fatigue_log_file_.close();
    if (defect_log_file_.is_open()) defect_log_file_.close();
    if (debug_log_file_.is_open()) debug_log_file_.close();
}

// ============================================================
// 设置日志级别
// ============================================================
void Logger::SetLogLevel(LogLevel level) {
    current_level_ = level;
}

// ============================================================
// 写入 OCR 结果日志
// ============================================================
void Logger::LogOcrResult(const OcrResult& result) {
    std::ostringstream oss;
    oss << "[" << GetCurrentTimeString() << "] "
        << "识别文本: " << result.text
        << ", 置信度: " << std::fixed << std::setprecision(2) << result.confidence
        << ", 边界框: [" << result.bbox[0] << "," << result.bbox[1]
        << "," << result.bbox[2] << "," << result.bbox[3] << "]";

    std::lock_guard<std::mutex> lock(log_mutex_);
    log_queue_.push("[OCR] " + oss.str());
    log_cv_.notify_one();
}

// ============================================================
// 写入疲劳检测结果日志
// ============================================================
void Logger::LogFatigueResult(const FatigueResult& result) {
    std::ostringstream oss;
    oss << "[" << GetCurrentTimeString() << "] "
        << "检测类别: " << result.category
        << ", 置信度: " << std::fixed << std::setprecision(2) << result.confidence
        << ", 状态: " << (result.is_fatigue ? "疲劳" : "正常");

    std::lock_guard<std::mutex> lock(log_mutex_);
    log_queue_.push("[FATIGUE] " + oss.str());
    log_cv_.notify_one();
}

// ============================================================
// 写入缺陷检测结果日志
// ============================================================
void Logger::LogDefectResult(const DefectResult& result) {
    std::ostringstream oss;
    oss << "[" << GetCurrentTimeString() << "] "
        << "缺陷类型: " << result.defect_type
        << ", 置信度: " << std::fixed << std::setprecision(2) << result.confidence
        << ", 位置: [" << result.bbox[0] << "," << result.bbox[1]
        << "," << result.bbox[2] << "," << result.bbox[3] << "]";

    std::lock_guard<std::mutex> lock(log_mutex_);
    log_queue_.push("[DEFECT] " + oss.str());
    log_cv_.notify_one();
}

// ============================================================
// 写入调试日志
// ============================================================
void Logger::LogDebug(const char* file, int line, const char* format, ...) {
    if (current_level_ > LOG_DEBUG) return;

    va_list args;
    va_start(args, format);
    std::string msg = FormatString(format, args);
    va_end(args);

    std::ostringstream oss;
    oss << "[" << GetCurrentTimeString() << "] [DEBUG] "
        << file << ":" << line << " - " << msg;

    std::lock_guard<std::mutex> lock(log_mutex_);
    log_queue_.push(oss.str());
    log_cv_.notify_one();
}

void Logger::LogInfo(const char* file, int line, const char* format, ...) {
    if (current_level_ > LOG_INFO) return;

    va_list args;
    va_start(args, format);
    std::string msg = FormatString(format, args);
    va_end(args);

    std::ostringstream oss;
    oss << "[" << GetCurrentTimeString() << "] [INFO] "
        << file << ":" << line << " - " << msg;

    std::lock_guard<std::mutex> lock(log_mutex_);
    log_queue_.push(oss.str());
    log_cv_.notify_one();
}

void Logger::LogWarn(const char* file, int line, const char* format, ...) {
    if (current_level_ > LOG_WARN) return;

    va_list args;
    va_start(args, format);
    std::string msg = FormatString(format, args);
    va_end(args);

    std::ostringstream oss;
    oss << "[" << GetCurrentTimeString() << "] [WARN] "
        << file << ":" << line << " - " << msg;

    std::lock_guard<std::mutex> lock(log_mutex_);
    log_queue_.push(oss.str());
    log_cv_.notify_one();
}

void Logger::LogError(const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string msg = FormatString(format, args);
    va_end(args);

    std::ostringstream oss;
    oss << "[" << GetCurrentTimeString() << "] [ERROR] "
        << file << ":" << line << " - " << msg;

    std::lock_guard<std::mutex> lock(log_mutex_);
    log_queue_.push(oss.str());
    log_cv_.notify_one();
}

// ============================================================
// 日志写入线程
// ============================================================
void Logger::LogWriterThread() {
    while (running_) {
        std::unique_lock<std::mutex> lock(log_mutex_);

        // 等待新日志或停止信号
        log_cv_.wait(lock, [this]() {
            return !log_queue_.empty() || !running_;
        });

        // 处理所有待写入的日志
        while (!log_queue_.empty()) {
            std::string msg = log_queue_.front();
            log_queue_.pop();

            // 写入调试日志文件
            WriteLog(debug_log_file_, msg);

            // 根据前缀写入对应的日志文件
            if (msg.find("[OCR]") != std::string::npos) {
                WriteLog(ocr_log_file_, msg);
            } else if (msg.find("[FATIGUE]") != std::string::npos) {
                WriteLog(fatigue_log_file_, msg);
            } else if (msg.find("[DEFECT]") != std::string::npos) {
                WriteLog(defect_log_file_, msg);
            }
        }
    }
}

// ============================================================
// 写入日志到文件
// ============================================================
void Logger::WriteLog(std::ofstream& file, const std::string& message) {
    if (file.is_open()) {
        file << message << std::endl;
        file.flush();
    }
}

// ============================================================
// 格式化字符串
// ============================================================
std::string Logger::FormatString(const char* format, va_list args) {
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    return std::string(buffer);
}
