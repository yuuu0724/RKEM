#ifndef IPC_MANAGER_H
#define IPC_MANAGER_H

#include "common.h"
#include <string>
#include <map>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>

// ============================================================
// 进程间通信管理器
// ============================================================
// 使用 Unix Domain Socket 实现进程间通信
// 支持消息发送和接收
// 支持消息回调处理
// ============================================================

class IpcManager {
public:
    IpcManager();
    ~IpcManager();

    // 初始化 IPC 管理器（服务端）
    // socket_path: Unix Socket 文件路径
    bool InitServer(const std::string& socket_path);

    // 等待客户端连接（服务端）
    bool AcceptConnection(int timeout_ms = 10000);

    // 初始化 IPC 管理器（客户端）
    // socket_path: Unix Socket 文件路径
    bool InitClient(const std::string& socket_path);

    // 关闭 IPC 连接
    void Shutdown();

    // 发送消息
    bool SendMessage(const IpcMessage& message);

    // 发送图像帧
    bool SendFrame(const ImageFrame& frame);

    // 发送检测结果
    bool SendOcrResult(const OcrResult& result);
    bool SendFatigueResult(const FatigueResult& result);
    bool SendDefectResult(const DefectResult& result);

    // 注册消息回调
    void RegisterCallback(IpcMessageType type,
                         std::function<void(const IpcMessage&)> callback);

    // 检查是否已连接
    bool IsConnected() const { return connected_; }

private:
    // 接收消息线程
    void ReceiveThread();

    // 处理接收到的消息
    void HandleMessage(const IpcMessage& message);

    // 发送数据
    bool SendData(const void* data, size_t length);

    // 接收数据
    bool RecvData(void* data, size_t length);

private:
    int server_fd_;                          // 服务端文件描述符
    int client_fd_;                          // 客户端文件描述符
    std::string socket_path_;                // Socket 文件路径
    bool is_server_;                         // 是否是服务端
    std::atomic<bool> connected_;            // 连接状态
    std::atomic<bool> running_;              // 运行标志

    std::thread recv_thread_;                // 接收线程
    std::mutex send_mutex_;                  // 发送互斥锁

    // 消息回调映射
    std::map<IpcMessageType, std::function<void(const IpcMessage&)>> callbacks_;
    std::mutex callback_mutex_;              // 回调互斥锁
};

#endif // IPC_MANAGER_H
