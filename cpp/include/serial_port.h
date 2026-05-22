#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

#include "common.h"
#include <string>
#include <mutex>
#include <atomic>

// ============================================================
// 串口通信接口
// ============================================================
// 支持串口的打开、关闭、发送、接收
// 支持配置串口参数
// 预留接口，用于后续扩展
// ============================================================

class SerialPort {
public:
    SerialPort();
    ~SerialPort();

    // 打开串口
    bool Open(const SerialConfig& config);

    // 关闭串口
    void Close();

    // 发送数据
    int Send(const uint8_t* data, size_t len);

    // 接收数据
    // timeout_ms: 超时时间（毫秒），-1 表示阻塞
    int Receive(uint8_t* buffer, size_t len, int timeout_ms = -1);

    // 检查串口是否打开
    bool IsOpen() const { return fd_ >= 0; }

    // 获取串口配置
    const SerialConfig& GetConfig() const { return config_; }

private:
    // 配置串口参数
    bool Configure();

private:
    int fd_;                                 // 文件描述符
    SerialConfig config_;                    // 串口配置
    std::mutex send_mutex_;                  // 发送互斥锁
    std::mutex recv_mutex_;                  // 接收互斥锁
};

#endif // SERIAL_PORT_H
