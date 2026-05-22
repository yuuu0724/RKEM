#include "camera.h"
#include "logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <opencv2/opencv.hpp>

// ============================================================
// 构造函数和析构函数
// ============================================================
Camera::Camera()
    : width_(0),
      height_(0),
      fd_(-1),
      capture_(nullptr),
      capturing_(false),
      running_(false) {
}

Camera::~Camera() {
    Release();
}

// ============================================================
// 初始化摄像头
// ============================================================
bool Camera::Init(const std::string& device, int width, int height) {
    device_ = device;
    width_ = width;
    height_ = height;

    // 打开摄像头
    if (!OpenCamera()) {
        return false;
    }

    LOG_INFO("Camera initialized: %s (%dx%d)", device.c_str(), width, height);
    return true;
}

// ============================================================
// 释放资源
// ============================================================
void Camera::Release() {
    StopCapture();
    CloseCamera();

    // 清空帧队列
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!frame_queue_.empty()) {
        frame_queue_.pop();
    }
}

// ============================================================
// 开始采集
// ============================================================
bool Camera::StartCapture() {
    if (capturing_) {
        return true;
    }

    if (!OpenCamera()) {
        return false;
    }

    running_ = true;
    capturing_ = true;

    // 启动采集线程
    capture_thread_ = std::thread(&Camera::CaptureThread, this);

    LOG_INFO("Camera capture started");
    return true;
}

// ============================================================
// 停止采集
// ============================================================
void Camera::StopCapture() {
    running_ = false;
    capturing_ = false;
    queue_cv_.notify_all();

    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }

    LOG_INFO("Camera capture stopped");
}

// ============================================================
// 获取一帧图像
// ============================================================
bool Camera::GetFrame(ImageFrame& frame, int timeout_ms) {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    // 等待帧或超时
    bool success = queue_cv_.wait_for(lock,
        std::chrono::milliseconds(timeout_ms),
        [this]() { return !frame_queue_.empty() || !running_; });

    if (!success || frame_queue_.empty()) {
        return false;
    }

    // 获取帧
    frame = frame_queue_.front();
    frame_queue_.pop();

    return true;
}

// ============================================================
// 采集线程函数
// ============================================================
void Camera::CaptureThread() {
    LOG_INFO("Camera capture thread started");
    int failed_reads = 0;

    while (running_) {
        cv::Mat cv_frame;

        // 读取一帧
        cv::VideoCapture* cap = static_cast<cv::VideoCapture*>(capture_);
        if (!cap->read(cv_frame)) {
            failed_reads++;
            if (failed_reads == 1 || failed_reads % 30 == 0) {
                LOG_WARN("Failed to read frame from camera %s (consecutive failures: %d)",
                         device_.c_str(), failed_reads);
            }
            if (failed_reads >= 5) {
                LOG_ERROR("Camera %s stopped after too many consecutive read failures", device_.c_str());
                running_ = false;
                capturing_ = false;
                queue_cv_.notify_all();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        failed_reads = 0;

        // 转换为 RGBA
        cv::Mat rgba_frame;
        if (cv_frame.channels() == 3) {
            cv::cvtColor(cv_frame, rgba_frame, cv::COLOR_BGR2RGBA);
        } else if (cv_frame.channels() == 1) {
            cv::cvtColor(cv_frame, rgba_frame, cv::COLOR_GRAY2RGBA);
        } else {
            rgba_frame = cv_frame;
        }

        // 创建 ImageFrame
        ImageFrame frame;
        frame.width = rgba_frame.cols;
        frame.height = rgba_frame.rows;
        frame.channels = 4;
        frame.format = 0;  // RGBA
        frame.timestamp = GetCurrentTimestamp();
        frame.frame_id = 0;

        // 复制数据
        size_t data_size = frame.width * frame.height * frame.channels;
        frame.data = new uint8_t[data_size];
        memcpy(frame.data, rgba_frame.data, data_size);

        // 添加到队列
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);

            // 如果队列已满，移除最旧的帧
            if (frame_queue_.size() >= MAX_QUEUE_SIZE) {
                ImageFrame old_frame = frame_queue_.front();
                frame_queue_.pop();
                delete[] old_frame.data;
            }

            frame_queue_.push(frame);
        }

        // 通知等待线程
        queue_cv_.notify_one();

        // 控制帧率
        std::this_thread::sleep_for(std::chrono::milliseconds(30));  // ~30fps
    }

    LOG_INFO("Camera capture thread stopped");
}

// ============================================================
// 打开摄像头
// ============================================================
bool Camera::OpenCamera() {
    if (capture_) {
        return true;
    }

    cv::VideoCapture* cap = nullptr;
    LOG_INFO("Opening camera: %s", device_.c_str());

    // 判断是设备文件还是 RTSP URL
    if (device_.find("rtsp://") == 0 || device_.find("http://") == 0) {
        // RTSP/HTTP 流
        cap = new cv::VideoCapture(device_);
    } else if (device_.find("/dev/video") == 0) {
        // USB 摄像头
        cap = new cv::VideoCapture(device_, cv::CAP_V4L2);
        if (!cap->isOpened()) {
            delete cap;
            int device_id = std::stoi(device_.substr(10));
            cap = new cv::VideoCapture(device_id, cv::CAP_V4L2);
        }
    } else {
        // 尝试作为设备 ID
        try {
            int device_id = std::stoi(device_);
            cap = new cv::VideoCapture(device_id, cv::CAP_V4L2);
        } catch (...) {
            LOG_ERROR("Invalid camera device: %s", device_.c_str());
            return false;
        }
    }

    if (!cap->isOpened()) {
        LOG_ERROR("Failed to open camera: %s", device_.c_str());
        delete cap;
        return false;
    }

    // video23/JR02 exposes MJPG but it is unreliable with this OpenCV/V4L2 stack.
    // Keep it on YUYV and use MJPG for the other USB camera to reduce bus load.
    bool use_yuyv = (device_ == "/dev/video23" || device_ == "23");
    const char* requested_format = use_yuyv ? "YUYV" : "MJPG";
    int fourcc = use_yuyv
        ? cv::VideoWriter::fourcc('Y', 'U', 'Y', 'V')
        : cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    cap->set(cv::CAP_PROP_FOURCC, fourcc);
    cap->set(cv::CAP_PROP_FRAME_WIDTH, width_);
    cap->set(cv::CAP_PROP_FRAME_HEIGHT, height_);
    cap->set(cv::CAP_PROP_FPS, 30);
    cap->set(cv::CAP_PROP_BUFFERSIZE, 1);
    LOG_INFO("Camera requested format: %s %dx%d @ %.0f fps",
             requested_format, width_, height_, cap->get(cv::CAP_PROP_FPS));

    capture_ = cap;
    return true;
}

// ============================================================
// 关闭摄像头
// ============================================================
void Camera::CloseCamera() {
    if (capture_) {
        cv::VideoCapture* cap = static_cast<cv::VideoCapture*>(capture_);
        cap->release();
        delete cap;
        capture_ = nullptr;
    }
}
