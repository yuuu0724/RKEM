#include "common.h"
#include "logger.h"
#include "fatigue_engine.h"
#include "camera.h"
#include "ipc_manager.h"
#include "thread_pool.h"
#include <csignal>
#include <atomic>
#include <exception>
#include <cstdlib>
#include <opencv2/opencv.hpp>
#include <unistd.h>

// ============================================================
// 疲劳检测子进程
// ============================================================
// 职责：
// 1. 采集图像
// 2. 执行疲劳检测
// 3. 将结果发送给主进程
// ============================================================

static std::atomic<bool> g_running(true);

static void SignalHandler(int sig) {
    g_running = false;
}

static void ConfigureDisplayEnv() {
    if (!getenv("DISPLAY")) {
        setenv("DISPLAY", ":0", 0);
    }
    if (!getenv("XDG_RUNTIME_DIR")) {
        std::string runtime_dir = "/run/user/" + std::to_string(getuid());
        setenv("XDG_RUNTIME_DIR", runtime_dir.c_str(), 0);
    }
}

static bool ShowOpenCvFrame(const std::string& window_name, const ImageFrame& frame, bool enabled) {
    if (!enabled) {
        return true;
    }

    try {
        static bool window_created = false;
        if (!window_created) {
            ConfigureDisplayEnv();
            cv::namedWindow(window_name, cv::WINDOW_NORMAL);
            cv::resizeWindow(window_name, frame.width, frame.height);
            window_created = true;
        }

        cv::Mat rgba(frame.height, frame.width, CV_8UC4, frame.data);
        cv::Mat bgr;
        cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
        cv::imshow(window_name, bgr);

        int key = cv::waitKey(1);
        if (key == 27 || key == 'q' || key == 'Q') {
            return false;
        }
    } catch (const cv::Exception& e) {
        LOG_ERROR("OpenCV window error: %s", e.what());
        return false;
    }

    return true;
}

int main(int argc, char* argv[]) {
    // 注册信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    fprintf(stdout, "[INFO] Fatigue process starting...\n");
    fflush(stdout);

    // 初始化日志
    Logger::GetInstance().Init("logs");
    Logger::GetInstance().SetLogLevel(LOG_DEBUG);

    LOG_INFO("Fatigue process starting...");

    // 解析参数
    std::string camera_device = "/dev/video23";
    std::string model_path = "model/fatigue/fatigue_two_outputs_i8.rknn";
    std::string labels_path = "model/fatigue/dataset.txt";
    std::string ipc_socket = "/tmp/integrated_inspection_fatigue.sock";
    bool show_window = true;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--camera" && i + 1 < argc) {
            camera_device = argv[++i];
        } else if (arg == "--model" && i + 1 < argc) {
            model_path = argv[++i];
        } else if (arg == "--labels" && i + 1 < argc) {
            labels_path = argv[++i];
        } else if (arg == "--ipc" && i + 1 < argc) {
            ipc_socket = argv[++i];
        } else if (arg == "--no-window") {
            show_window = false;
        } else if (arg == "--show-window") {
            show_window = true;
        }
    }

    fprintf(stdout, "[INFO] Fatigue: camera=%s, ipc=%s\n", camera_device.c_str(), ipc_socket.c_str());
    fflush(stdout);

    // 初始化疲劳检测引擎
    fprintf(stdout, "[INFO] Initializing fatigue engine...\n");
    fflush(stdout);
    FatigueEngine fatigue_engine;
    if (!fatigue_engine.Init(model_path, labels_path)) {
        fprintf(stderr, "[ERROR] Failed to init fatigue engine\n");
        return 1;
    }

    // 初始化摄像头
    fprintf(stdout, "[INFO] Initializing camera: %s\n", camera_device.c_str());
    fflush(stdout);
    Camera camera;
    if (!camera.Init(camera_device, 640, 480)) {
        fprintf(stderr, "[ERROR] Failed to init camera\n");
        return 1;
    }

    // 初始化 IPC（客户端模式）
    fprintf(stdout, "[INFO] Connecting to IPC: %s\n", ipc_socket.c_str());
    fflush(stdout);
    IpcManager ipc;
    if (!ipc.InitClient(ipc_socket)) {
        fprintf(stderr, "[ERROR] Failed to init IPC client\n");
        return 1;
    }

    // 启动摄像头采集
    if (!camera.StartCapture()) {
        LOG_ERROR("Failed to start camera capture");
        return 1;
    }

    // 创建线程池
    ThreadPool thread_pool(2);
    thread_pool.Start();

    LOG_INFO("Fatigue process initialized successfully");

    // 主循环
    while (g_running) {
        // 获取一帧图像
        ImageFrame frame;
        if (!camera.GetFrame(frame, 1000)) {
            if (!camera.IsCapturing()) {
                LOG_ERROR("Camera stopped, exiting fatigue process");
                break;
            }
            continue;
        }

        if (!ShowOpenCvFrame("Fatigue Camera", frame, show_window)) {
            delete[] frame.data;
            g_running = false;
            break;
        }

        if (thread_pool.GetTaskCount() > 0) {
            delete[] frame.data;
            continue;
        }

        // 提交疲劳检测任务到线程池
        try {
            thread_pool.Submit([&fatigue_engine, &ipc, frame]() {
                FatigueResult result;

                // 执行疲劳检测
                if (fatigue_engine.Detect(frame.data, frame.width, frame.height,
                                          frame.channels, result)) {
                    // 发送结果
                    ipc.SendFatigueResult(result);
                    Logger::GetInstance().LogFatigueResult(result);
                }

                // 释放帧数据
                delete[] frame.data;
            });
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to submit fatigue task: %s", e.what());
            delete[] frame.data;
            break;
        }
    }

    // 清理
    LOG_INFO("Fatigue process shutting down...");
    thread_pool.Stop();
    camera.StopCapture();
    ipc.Shutdown();
    fatigue_engine.Release();
    if (show_window) {
        cv::destroyWindow("Fatigue Camera");
    }
    Logger::GetInstance().Shutdown();

    return 0;
}
