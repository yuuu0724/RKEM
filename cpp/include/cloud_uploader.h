#ifndef CLOUD_UPLOADER_H
#define CLOUD_UPLOADER_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

struct CloudUploadConfig {
    bool enabled = false;
    std::string device_id = "rk3588-001";
    std::string server_url = "http://192.168.31.78:8080";
    std::string rk3588_ip = "192.168.31.23";
    std::string program = "main_process";
    std::string version = "1.0.0";
    int heartbeat_interval_sec = 5;
    int request_timeout_sec = 3;
    std::string chip_camera = "/dev/video21";
    std::string fatigue_camera = "/dev/video23";
    bool chip_camera_online = true;
    bool fatigue_camera_online = true;
};

class CloudUploader {
public:
    CloudUploader();
    ~CloudUploader();

    CloudUploader(const CloudUploader&) = delete;
    CloudUploader& operator=(const CloudUploader&) = delete;

    bool Start(const std::string& config_path);
    void Stop();

    void UpdateCameraStatus(bool chip_camera_online, bool fatigue_camera_online);
    void PostInspectionResult(const std::string& module,
                              const std::string& camera,
                              const std::string& result_json);
    void PostAlarm(const std::string& module,
                   const std::string& level,
                   const std::string& alarm_type,
                   const std::string& message,
                   const std::string& result_json);

    bool enabled() const { return config_.enabled; }

    static std::string JsonString(const std::string& value);
    static std::string NowUtcIso();

private:
    struct UploadTask {
        std::string method;
        std::string path;
        std::string payload;
    };

    void WorkerLoop();
    void Enqueue(const UploadTask& task);
    void PostHeartbeat();
    bool SendRequest(const UploadTask& task);

    CloudUploadConfig config_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<UploadTask> queue_;
};

#endif // CLOUD_UPLOADER_H
