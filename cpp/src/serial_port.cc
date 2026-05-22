#include "serial_port.h"
#include "logger.h"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <sys/select.h>

// ============================================================
// 构造函数和析构函数
// ============================================================
SerialPort::SerialPort()
    : fd_(-1) {
    memset(&config_, 0, sizeof(config_));
}

SerialPort::~SerialPort() {
    Close();
}

// ============================================================
// 打开串口
// ============================================================
bool SerialPort::Open(const SerialConfig& config) {
    config_ = config;

    // 打开串口设备
    fd_ = open(config.port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd_ < 0) {
        LOG_ERROR("Failed to open serial port %s: %s",
                  config.port.c_str(), strerror(errno));
        return false;
    }

    // 配置串口参数
    if (!Configure()) {
        Close();
        return false;
    }

    LOG_INFO("Serial port %s opened successfully", config.port.c_str());
    return true;
}

// ============================================================
// 关闭串口
// ============================================================
void SerialPort::Close() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
        LOG_INFO("Serial port closed");
    }
}

// ============================================================
// 发送数据
// ============================================================
int SerialPort::Send(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    if (fd_ < 0) {
        LOG_ERROR("Serial port not open");
        return -1;
    }

    ssize_t written = write(fd_, data, len);
    if (written < 0) {
        LOG_ERROR("Failed to send data: %s", strerror(errno));
        return -1;
    }

    return static_cast<int>(written);
}

// ============================================================
// 接收数据
// ============================================================
int SerialPort::Receive(uint8_t* buffer, size_t len, int timeout_ms) {
    std::lock_guard<std::mutex> lock(recv_mutex_);

    if (fd_ < 0) {
        LOG_ERROR("Serial port not open");
        return -1;
    }

    if (timeout_ms < 0) {
        // 阻塞读取
        ssize_t n = read(fd_, buffer, len);
        if (n < 0) {
            LOG_ERROR("Failed to receive data: %s", strerror(errno));
            return -1;
        }
        return static_cast<int>(n);
    }

    // 使用 select 实现超时
    fd_set read_fds;
    struct timeval timeout;

    FD_ZERO(&read_fds);
    FD_SET(fd_, &read_fds);

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
    if (ret < 0) {
        LOG_ERROR("Select failed: %s", strerror(errno));
        return -1;
    } else if (ret == 0) {
        // 超时
        return 0;
    }

    // 读取数据
    ssize_t n = read(fd_, buffer, len);
    if (n < 0) {
        LOG_ERROR("Failed to receive data: %s", strerror(errno));
        return -1;
    }

    return static_cast<int>(n);
}

// ============================================================
// 配置串口参数
// ============================================================
bool SerialPort::Configure() {
    struct termios options;

    // 获取当前配置
    if (tcgetattr(fd_, &options) < 0) {
        LOG_ERROR("Failed to get terminal attributes: %s", strerror(errno));
        return false;
    }

    // 设置波特率
    speed_t baudrate;
    switch (config_.baudrate) {
        case 9600:   baudrate = B9600;   break;
        case 19200:  baudrate = B19200;  break;
        case 38400:  baudrate = B38400;  break;
        case 57600:  baudrate = B57600;  break;
        case 115200: baudrate = B115200; break;
        case 230400: baudrate = B230400; break;
        case 460800: baudrate = B460800; break;
        case 921600: baudrate = B921600; break;
        default:
            LOG_ERROR("Unsupported baudrate: %d", config_.baudrate);
            return false;
    }

    cfsetispeed(&options, baudrate);
    cfsetospeed(&options, baudrate);

    // 设置数据位
    options.c_cflag &= ~CSIZE;
    switch (config_.databits) {
        case 5: options.c_cflag |= CS5; break;
        case 6: options.c_cflag |= CS6; break;
        case 7: options.c_cflag |= CS7; break;
        case 8: options.c_cflag |= CS8; break;
        default:
            LOG_ERROR("Unsupported databits: %d", config_.databits);
            return false;
    }

    // 设置停止位
    if (config_.stopbits == 1) {
        options.c_cflag &= ~CSTOPB;
    } else if (config_.stopbits == 2) {
        options.c_cflag |= CSTOPB;
    } else {
        LOG_ERROR("Unsupported stopbits: %d", config_.stopbits);
        return false;
    }

    // 设置校验位
    switch (config_.parity) {
        case 'N':
        case 'n':
            options.c_cflag &= ~PARENB;
            options.c_cflag &= ~PARODD;
            break;
        case 'E':
        case 'e':
            options.c_cflag |= PARENB;
            options.c_cflag &= ~PARODD;
            break;
        case 'O':
        case 'o':
            options.c_cflag |= PARENB;
            options.c_cflag |= PARODD;
            break;
        default:
            LOG_ERROR("Unsupported parity: %c", config_.parity);
            return false;
    }

    // 设置为原始模式
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;

    // 设置超时
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 10;  // 1 秒超时

    // 应用配置
    if (tcsetattr(fd_, TCSANOW, &options) < 0) {
        LOG_ERROR("Failed to set terminal attributes: %s", strerror(errno));
        return false;
    }

    // 清空缓冲区
    tcflush(fd_, TCIOFLUSH);

    return true;
}
