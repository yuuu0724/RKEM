#include "common.h"
#include "logger.h"
#include "defect_engine.h"
#include "camera.h"
#include "ipc_manager.h"
#include "thread_pool.h"
#include <csignal>
#include <atomic>
#include <exception>
#include <unistd.h>

// ============================================================
// 缺陷检测子进程（预留）
// ============================================================
// 职责：
// 1. 采集图像
// 2. 执行缺陷检测
// 3. 将结果发送给主进程
// ============================================================

static std::atomic<bool> g_running(true);

static void SignalHandler(int sig) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    // 注册信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // 初始化日志
    Logger::GetInstance().Init("../logs");
    Logger::GetInstance().SetLogLevel(LOG_DEBUG);

    LOG_INFO("Defect process starting...");

    // 解析参数
    std::string camera_device = "/dev/video0";
    std::string model_path = "model/defect/defect.rknn";
    std::string labels_path = "model/defect/dataset.txt";
    std::string ipc_socket = "/tmp/integrated_inspection_defect.sock";

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
        }
    }

    // 初始化缺陷检测引擎
    DefectEngine defect_engine;
    if (!defect_engine.Init(model_path, labels_path)) {
        LOG_ERROR("Failed to init defect engine");
        return 1;
    }

    // 初始化摄像头
    Camera camera;
    if (!camera.Init(camera_device, 640, 480)) {
        LOG_ERROR("Failed to init camera");
        return 1;
    }

    // 初始化 IPC（客户端模式）
    IpcManager ipc;
    if (!ipc.InitClient(ipc_socket)) {
        LOG_ERROR("Failed to init IPC client");
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

    LOG_INFO("Defect process initialized successfully");

    // 主循环
    while (g_running) {
        // 获取一帧图像
        ImageFrame frame;
        if (!camera.GetFrame(frame, 1000)) {
            if (!camera.IsCapturing()) {
                LOG_ERROR("Camera stopped, exiting defect process");
                break;
            }
            continue;
        }

        if (thread_pool.GetTaskCount() > 0) {
            delete[] frame.data;
            continue;
        }

        // 提交缺陷检测任务到线程池
        try {
            thread_pool.Submit([&defect_engine, &ipc, frame]() {
                std::vector<DefectResult> results;

                // 执行缺陷检测
                if (defect_engine.Detect(frame.data, frame.width, frame.height,
                                         frame.channels, results)) {
                    // 发送结果
                    for (const auto& result : results) {
                        ipc.SendDefectResult(result);
                        Logger::GetInstance().LogDefectResult(result);
                    }
                }

                // 释放帧数据
                delete[] frame.data;
            });
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to submit defect task: %s", e.what());
            delete[] frame.data;
            break;
        }
    }

    // 清理
    LOG_INFO("Defect process shutting down...");
    thread_pool.Stop();
    camera.StopCapture();
    ipc.Shutdown();
    defect_engine.Release();
    Logger::GetInstance().Shutdown();

    return 0;
}
