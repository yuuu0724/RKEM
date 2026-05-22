#include "ipc_manager.h"
#include "logger.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

// ============================================================
// 构造函数和析构函数
// ============================================================
IpcManager::IpcManager()
    : server_fd_(-1),
      client_fd_(-1),
      is_server_(false),
      connected_(false),
      running_(false) {
}

IpcManager::~IpcManager() {
    Shutdown();
}

// ============================================================
// 初始化服务端
// ============================================================
bool IpcManager::InitServer(const std::string& socket_path) {
    socket_path_ = socket_path;
    is_server_ = true;

    // 创建 Unix Socket
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return false;
    }

    // 绑定地址
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    // 删除已存在的 socket 文件
    unlink(socket_path.c_str());

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind socket: %s", strerror(errno));
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // 监听连接
    if (listen(server_fd_, 1) < 0) {
        LOG_ERROR("Failed to listen on socket: %s", strerror(errno));
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    fprintf(stdout, "[INFO] IPC server initialized on %s\n", socket_path.c_str());
    fflush(stdout);
    return true;
}

// ============================================================
// 等待客户端连接
// ============================================================
bool IpcManager::AcceptConnection(int timeout_ms) {
    if (!is_server_ || server_fd_ < 0) {
        fprintf(stderr, "[ERROR] Not a server or server not initialized\n");
        return false;
    }

    fprintf(stdout, "[INFO] Waiting for client connection...\n");
    fflush(stdout);

    // 使用 select 实现超时
    fd_set read_fds;
    struct timeval timeout;

    FD_ZERO(&read_fds);
    FD_SET(server_fd_, &read_fds);

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(server_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
    if (ret < 0) {
        fprintf(stderr, "[ERROR] Select failed: %s\n", strerror(errno));
        return false;
    } else if (ret == 0) {
        fprintf(stderr, "[WARN] Accept timeout\n");
        return false;
    }

    // 接受连接
    client_fd_ = accept(server_fd_, nullptr, nullptr);
    if (client_fd_ < 0) {
        fprintf(stderr, "[ERROR] Accept failed: %s\n", strerror(errno));
        return false;
    }

    connected_ = true;
    running_ = true;

    // 启动接收线程
    recv_thread_ = std::thread(&IpcManager::ReceiveThread, this);

    fprintf(stdout, "[INFO] Client connected\n");
    fflush(stdout);
    return true;
}

// ============================================================
// 初始化客户端
// ============================================================
bool IpcManager::InitClient(const std::string& socket_path) {
    socket_path_ = socket_path;
    is_server_ = false;

    // 创建 Unix Socket
    client_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd_ < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return false;
    }

    // 连接服务端
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(client_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to connect to server: %s", strerror(errno));
        close(client_fd_);
        client_fd_ = -1;
        return false;
    }

    connected_ = true;
    running_ = true;

    // 启动接收线程
    recv_thread_ = std::thread(&IpcManager::ReceiveThread, this);

    LOG_INFO("IPC client connected to %s", socket_path.c_str());
    return true;
}

// ============================================================
// 关闭 IPC 连接
// ============================================================
void IpcManager::Shutdown() {
    running_ = false;
    connected_ = false;

    if (client_fd_ >= 0) {
        shutdown(client_fd_, SHUT_RDWR);
    }

    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
    }

    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }

    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }

    if (client_fd_ >= 0) {
        close(client_fd_);
        client_fd_ = -1;
    }

    // 删除 socket 文件
    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
    }

    LOG_INFO("IPC manager shutdown");
}

// ============================================================
// 发送消息
// ============================================================
bool IpcManager::SendMessage(const IpcMessage& message) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    int fd = is_server_ ? client_fd_ : client_fd_;
    if (fd < 0 || !connected_) {
        return false;
    }

    // 发送消息头
    if (!SendData(&message.header, sizeof(message.header))) {
        return false;
    }

    // 发送消息载荷
    if (!message.payload.empty()) {
        if (!SendData(message.payload.data(), message.payload.size())) {
            return false;
        }
    }

    return true;
}

// ============================================================
// 发送图像帧
// ============================================================
bool IpcManager::SendFrame(const ImageFrame& frame) {
    IpcMessage message;
    message.header.type = IPC_MSG_FRAME;
    message.header.timestamp = frame.timestamp;

    // 计算载荷大小
    size_t data_size = frame.width * frame.height * frame.channels;
    size_t header_size = sizeof(int) * 4 + sizeof(uint64_t);  // width, height, channels, format, timestamp
    message.header.length = header_size + data_size;

    // 序列化帧头
    message.payload.resize(header_size);
    memcpy(message.payload.data(), &frame.width, sizeof(int));
    memcpy(message.payload.data() + sizeof(int), &frame.height, sizeof(int));
    memcpy(message.payload.data() + sizeof(int) * 2, &frame.channels, sizeof(int));
    memcpy(message.payload.data() + sizeof(int) * 3, &frame.format, sizeof(int));
    memcpy(message.payload.data() + sizeof(int) * 4, &frame.timestamp, sizeof(uint64_t));

    // 追加帧数据
    message.payload.insert(message.payload.end(), frame.data, frame.data + data_size);

    return SendMessage(message);
}

// ============================================================
// 发送检测结果
// ============================================================
bool IpcManager::SendOcrResult(const OcrResult& result) {
    IpcMessage message;
    message.header.type = IPC_MSG_RESULT;
    message.header.timestamp = result.timestamp;

    // 序列化结果
    std::ostringstream oss;
    oss << "OCR:" << result.text << "," << result.confidence << ","
        << result.bbox[0] << "," << result.bbox[1] << ","
        << result.bbox[2] << "," << result.bbox[3];

    std::string data = oss.str();
    message.header.length = data.size();
    message.payload.assign(data.begin(), data.end());

    return SendMessage(message);
}

bool IpcManager::SendFatigueResult(const FatigueResult& result) {
    IpcMessage message;
    message.header.type = IPC_MSG_RESULT;
    message.header.timestamp = result.timestamp;

    // 序列化结果
    std::ostringstream oss;
    oss << "FATIGUE:" << result.category << "," << result.confidence << ","
        << (result.is_fatigue ? 1 : 0);

    std::string data = oss.str();
    message.header.length = data.size();
    message.payload.assign(data.begin(), data.end());

    return SendMessage(message);
}

bool IpcManager::SendDefectResult(const DefectResult& result) {
    IpcMessage message;
    message.header.type = IPC_MSG_RESULT;
    message.header.timestamp = result.timestamp;

    // 序列化结果
    std::ostringstream oss;
    oss << "DEFECT:" << result.defect_type << "," << result.confidence << ","
        << result.bbox[0] << "," << result.bbox[1] << ","
        << result.bbox[2] << "," << result.bbox[3];

    std::string data = oss.str();
    message.header.length = data.size();
    message.payload.assign(data.begin(), data.end());

    return SendMessage(message);
}

// ============================================================
// 注册消息回调
// ============================================================
void IpcManager::RegisterCallback(IpcMessageType type,
                                  std::function<void(const IpcMessage&)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callbacks_[type] = callback;
}

// ============================================================
// 接收消息线程
// ============================================================
void IpcManager::ReceiveThread() {
    while (running_ && connected_) {
        IpcMessage message;

        // 接收消息头
        if (!RecvData(&message.header, sizeof(message.header))) {
            LOG_ERROR("Failed to receive message header");
            break;
        }

        // 接收消息载荷
        if (message.header.length > 0) {
            message.payload.resize(message.header.length);
            if (!RecvData(message.payload.data(), message.header.length)) {
                LOG_ERROR("Failed to receive message payload");
                break;
            }
        }

        // 处理消息
        HandleMessage(message);
    }

    connected_ = false;
    LOG_INFO("IPC receive thread stopped");
}

// ============================================================
// 处理接收到的消息
// ============================================================
void IpcManager::HandleMessage(const IpcMessage& message) {
    std::lock_guard<std::mutex> lock(callback_mutex_);

    auto it = callbacks_.find(message.header.type);
    if (it != callbacks_.end()) {
        try {
            it->second(message);
        } catch (const std::exception& e) {
            LOG_ERROR("Callback exception: %s", e.what());
        }
    }
}

// ============================================================
// 发送数据
// ============================================================
bool IpcManager::SendData(const void* data, size_t length) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t remaining = length;

    while (remaining > 0) {
        ssize_t sent = send(client_fd_, ptr, remaining, 0);
        if (sent <= 0) {
            LOG_ERROR("Send failed: %s", strerror(errno));
            return false;
        }
        ptr += sent;
        remaining -= sent;
    }

    return true;
}

// ============================================================
// 接收数据
// ============================================================
bool IpcManager::RecvData(void* data, size_t length) {
    uint8_t* ptr = static_cast<uint8_t*>(data);
    size_t remaining = length;

    while (remaining > 0) {
        ssize_t received = recv(client_fd_, ptr, remaining, 0);
        if (received <= 0) {
            if (received == 0) {
                LOG_INFO("Connection closed by peer");
            } else {
                LOG_ERROR("Receive failed: %s", strerror(errno));
            }
            return false;
        }
        ptr += received;
        remaining -= received;
    }

    return true;
}
