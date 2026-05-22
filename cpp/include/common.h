#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>

// ============================================================
// 日志级别定义
// ============================================================
enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
};

// ============================================================
// NPU 核心掩码定义
// ============================================================
enum NpuCoreMask {
    NPU_CORE_0 = 0,    // RKNN_NPU_CORE_0
    NPU_CORE_1 = 1,    // RKNN_NPU_CORE_1
    NPU_CORE_2 = 2,    // RKNN_NPU_CORE_2
    NPU_CORE_AUTO = 7  // RKNN_NPU_CORE_AUTO
};

// ============================================================
// 检测结果结构体
// ============================================================

// OCR 识别结果
struct OcrResult {
    std::string text;              // 识别文本
    float confidence;              // 置信度
    int bbox[4];                   // 边界框 [x1, y1, x2, y2]
    uint64_t timestamp;            // 时间戳
};

// 疲劳检测结果
struct FatigueResult {
    std::string category;          // 检测类别 (open_eye, open_mouth, close_eye)
    float confidence;              // 置信度
    bool is_fatigue;               // 是否疲劳
    uint64_t timestamp;            // 时间戳
};

// 缺陷检测结果 (预留)
struct DefectResult {
    std::string defect_type;       // 缺陷类型
    float confidence;              // 置信度
    int bbox[4];                   // 边界框 [x, y, w, h]
    uint64_t timestamp;            // 时间戳
};

// ============================================================
// 图像帧结构体
// ============================================================
struct ImageFrame {
    uint8_t* data;                 // 图像数据
    int width;                     // 宽度
    int height;                    // 高度
    int channels;                  // 通道数
    int format;                    // 像素格式
    uint64_t timestamp;            // 时间戳
    int frame_id;                  // 帧 ID
};

// ============================================================
// 串口配置结构体
// ============================================================
struct SerialConfig {
    std::string port;              // 串口设备路径，如 /dev/ttyS0
    int baudrate;                  // 波特率，如 115200
    int databits;                  // 数据位，如 8
    int stopbits;                  // 停止位，如 1
    char parity;                   // 校验位，如 'N'
};

// ============================================================
// 进程间通信消息类型
// ============================================================
enum IpcMessageType {
    IPC_MSG_FRAME = 0,             // 图像帧消息
    IPC_MSG_RESULT = 1,            // 检测结果消息
    IPC_MSG_COMMAND = 2,           // 命令消息
    IPC_MSG_STATUS = 3             // 状态消息
};

// IPC 消息头
struct IpcMessageHeader {
    IpcMessageType type;           // 消息类型
    uint32_t length;               // 消息长度
    uint64_t timestamp;            // 时间戳
};

// IPC 消息
struct IpcMessage {
    IpcMessageHeader header;       // 消息头
    std::vector<uint8_t> payload;  // 消息载荷
};

// ============================================================
// 线程池任务类型
// ============================================================
typedef std::function<void()> Task;

// ============================================================
// 回调函数类型定义
// ============================================================
typedef std::function<void(const OcrResult&)> OcrResultCallback;
typedef std::function<void(const FatigueResult&)> FatigueResultCallback;
typedef std::function<void(const DefectResult&)> DefectResultCallback;

// ============================================================
// 工具函数
// ============================================================

// 获取当前时间戳（微秒）
inline uint64_t GetCurrentTimestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

// 获取当前时间字符串
inline std::string GetCurrentTimeString() {
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
}

#endif // COMMON_H
