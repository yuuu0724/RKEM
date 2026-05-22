#ifndef CAMERA_H
#define CAMERA_H

#include "common.h"
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>

// ============================================================
// 摄像头采集模块
// ============================================================
// 支持 USB 摄像头和 RTSP 流
// 支持多线程采集
// 支持帧缓冲队列
// ============================================================

class Camera {
public:
    Camera();
    ~Camera();

    // 初始化摄像头
    // device: 设备路径或 RTSP URL
    // width: 期望宽度
    // height: 期望高度
    bool Init(const std::string& device, int width = 640, int height = 480);

    // 释放资源
    void Release();

    // 开始采集
    bool StartCapture();

    // 停止采集
    void StopCapture();

    // 获取一帧图像
    // timeout_ms: 超时时间（毫秒）
    bool GetFrame(ImageFrame& frame, int timeout_ms = 1000);

    // 获取图像宽度
    int GetWidth() const { return width_; }

    // 获取图像高度
    int GetHeight() const { return height_; }

    // 检查是否正在采集
    bool IsCapturing() const { return capturing_; }

private:
    // 采集线程函数
    void CaptureThread();

    // 打开摄像头
    bool OpenCamera();

    // 关闭摄像头
    void CloseCamera();

private:
    std::string device_;                     // 设备路径或 URL
    int width_;                              // 图像宽度
    int height_;                             // 图像高度
    int fd_;                                 // 设备文件描述符
    void* capture_;                          // OpenCV VideoCapture

    std::thread capture_thread_;             // 采集线程
    std::atomic<bool> capturing_;            // 采集标志
    std::atomic<bool> running_;              // 运行标志

    // 帧缓冲队列
    std::queue<ImageFrame> frame_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    static const int MAX_QUEUE_SIZE = 5;     // 最大队列大小
};

#endif // CAMERA_H
