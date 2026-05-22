#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"
#include <string>
#include <fstream>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>

// ============================================================
// 日志系统
// ============================================================
// 支持多线程安全的日志写入
// 支持多个日志文件（OCR、疲劳检测、缺陷检测、调试）
// 支持异步日志写入
// ============================================================

class Logger {
public:
    // 获取单例实例
    static Logger& GetInstance();

    // 初始化日志系统
    bool Init(const std::string& log_dir);

    // 关闭日志系统
    void Shutdown();

    // 设置日志级别
    void SetLogLevel(LogLevel level);

    // 写入 OCR 结果日志
    void LogOcrResult(const OcrResult& result);

    // 写入疲劳检测结果日志
    void LogFatigueResult(const FatigueResult& result);

    // 写入缺陷检测结果日志
    void LogDefectResult(const DefectResult& result);

    // 写入调试日志
    void LogDebug(const char* file, int line, const char* format, ...);
    void LogInfo(const char* file, int line, const char* format, ...);
    void LogWarn(const char* file, int line, const char* format, ...);
    void LogError(const char* file, int line, const char* format, ...);

private:
    Logger();
    ~Logger();

    // 禁止拷贝
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // 日志写入线程函数
    void LogWriterThread();

    // 写入日志到文件
    void WriteLog(std::ofstream& file, const std::string& message);

    // 格式化字符串
    std::string FormatString(const char* format, va_list args);

private:
    std::string log_dir_;                    // 日志目录
    std::ofstream ocr_log_file_;             // OCR 日志文件
    std::ofstream fatigue_log_file_;         // 疲劳检测日志文件
    std::ofstream defect_log_file_;          // 缺陷检测日志文件
    std::ofstream debug_log_file_;           // 调试日志文件

    std::mutex log_mutex_;                   // 日志互斥锁
    std::queue<std::string> log_queue_;      // 日志队列
    std::condition_variable log_cv_;         // 日志条件变量
    std::thread writer_thread_;              // 日志写入线程
    std::atomic<bool> running_;              // 运行标志

    LogLevel current_level_;                 // 当前日志级别
};

// ============================================================
// 日志宏定义
// ============================================================
#define LOG_DEBUG(fmt, ...) \
    Logger::GetInstance().LogDebug(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    Logger::GetInstance().LogInfo(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    Logger::GetInstance().LogWarn(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    Logger::GetInstance().LogError(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif // LOGGER_H
