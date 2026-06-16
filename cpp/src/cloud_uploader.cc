#include "cloud_uploader.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

struct ParsedUrl {
    std::string host;
    std::string port = "80";
    std::string base_path;
};

std::string trim(const std::string& value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool parseBool(const std::string& value, bool fallback)
{
    const std::string normalized = lower(trim(value));
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    return fallback;
}

int parseInt(const std::string& value, int fallback)
{
    try {
        return std::stoi(trim(value));
    } catch (...) {
        return fallback;
    }
}

std::map<std::string, std::string> readUploadSection(const std::string& path)
{
    std::ifstream file(path);
    std::map<std::string, std::string> values;
    if (!file.is_open()) {
        return values;
    }

    bool in_upload = false;
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            in_upload = lower(trim(line.substr(1, line.size() - 2))) == "upload";
            continue;
        }
        if (!in_upload) {
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        values[lower(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
    }
    return values;
}

std::string getValue(const std::map<std::string, std::string>& values,
                     const std::string& key,
                     const std::string& fallback)
{
    auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
}

bool parseUrl(const std::string& url, ParsedUrl& parsed)
{
    const std::string prefix = "http://";
    if (url.rfind(prefix, 0) != 0) {
        return false;
    }
    std::string rest = url.substr(prefix.size());
    const size_t slash = rest.find('/');
    std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    parsed.base_path = slash == std::string::npos ? "" : rest.substr(slash);
    if (parsed.base_path == "/") {
        parsed.base_path.clear();
    }

    const size_t colon = authority.rfind(':');
    if (colon == std::string::npos) {
        parsed.host = authority;
        parsed.port = "80";
    } else {
        parsed.host = authority.substr(0, colon);
        parsed.port = authority.substr(colon + 1);
    }
    return !parsed.host.empty() && !parsed.port.empty();
}

int connectWithTimeout(const std::string& host, const std::string& port, int timeout_sec)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    const int gai = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (gai != 0) {
        std::cerr << "[WARN] cloud upload getaddrinfo failed host=" << host
                  << " error=" << gai_strerror(gai) << "\n";
        return -1;
    }

    int sock = -1;
    for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) {
            continue;
        }

        const int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        int ret = connect(sock, rp->ai_addr, rp->ai_addrlen);
        if (ret < 0 && errno != EINPROGRESS) {
            close(sock);
            sock = -1;
            continue;
        }

        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(sock, &write_set);
        timeval tv{};
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        ret = select(sock + 1, nullptr, &write_set, nullptr, &tv);
        if (ret > 0 && FD_ISSET(sock, &write_set)) {
            int error = 0;
            socklen_t len = sizeof(error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);
            if (error == 0) {
                fcntl(sock, F_SETFL, flags);
                break;
            }
        }

        close(sock);
        sock = -1;
    }

    freeaddrinfo(result);
    return sock;
}

bool sendAll(int sock, const std::string& data)
{
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(sock, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

CloudUploadConfig loadConfig(const std::string& path)
{
    CloudUploadConfig config;
    const auto values = readUploadSection(path);
    if (values.empty()) {
        std::cerr << "[WARN] cloud upload config not found or empty: " << path << "\n";
        return config;
    }

    config.enabled = parseBool(getValue(values, "enabled", "true"), true);
    config.device_id = getValue(values, "device_id", config.device_id);
    config.server_url = getValue(values, "server_url", config.server_url);
    config.rk3588_ip = getValue(values, "rk3588_ip", config.rk3588_ip);
    config.program = getValue(values, "program", config.program);
    config.version = getValue(values, "version", config.version);
    config.heartbeat_interval_sec = parseInt(getValue(values, "heartbeat_interval_sec", "5"), 5);
    config.request_timeout_sec = parseInt(getValue(values, "request_timeout_sec", "3"), 3);
    config.chip_camera = getValue(values, "chip_camera", config.chip_camera);
    config.fatigue_camera = getValue(values, "fatigue_camera", config.fatigue_camera);
    config.chip_camera_online = parseBool(getValue(values, "chip_camera_online", "true"), true);
    config.fatigue_camera_online = parseBool(getValue(values, "fatigue_camera_online", "true"), true);
    return config;
}

} // namespace

CloudUploader::CloudUploader() = default;

CloudUploader::~CloudUploader()
{
    Stop();
}

bool CloudUploader::Start(const std::string& config_path)
{
    if (running_.load()) {
        return true;
    }

    config_ = loadConfig(config_path);
    if (!config_.enabled) {
        std::cerr << "[INFO] cloud upload disabled config=" << config_path << "\n";
        return false;
    }

    running_.store(true);
    worker_ = std::thread(&CloudUploader::WorkerLoop, this);
    std::cerr << "[INFO] cloud upload started server_url=" << config_.server_url
              << " device_id=" << config_.device_id << "\n";
    return true;
}

void CloudUploader::Stop()
{
    running_.store(false);
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void CloudUploader::UpdateCameraStatus(bool chip_camera_online, bool fatigue_camera_online)
{
    std::lock_guard<std::mutex> lock(mutex_);
    config_.chip_camera_online = chip_camera_online;
    config_.fatigue_camera_online = fatigue_camera_online;
}

void CloudUploader::PostInspectionResult(const std::string& module,
                                         const std::string& camera,
                                         const std::string& result_json)
{
    if (!config_.enabled) {
        return;
    }

    std::ostringstream payload;
    payload << "{"
            << "\"device_id\":" << JsonString(config_.device_id) << ","
            << "\"timestamp\":" << JsonString(NowUtcIso()) << ","
            << "\"module\":" << JsonString(module) << ","
            << "\"camera\":" << JsonString(camera) << ","
            << "\"result\":" << result_json
            << "}";
    Enqueue({"POST", "/api/inspection/result", payload.str()});
}

void CloudUploader::PostAlarm(const std::string& module,
                              const std::string& level,
                              const std::string& alarm_type,
                              const std::string& message,
                              const std::string& result_json)
{
    if (!config_.enabled) {
        return;
    }

    std::ostringstream payload;
    payload << "{"
            << "\"device_id\":" << JsonString(config_.device_id) << ","
            << "\"timestamp\":" << JsonString(NowUtcIso()) << ","
            << "\"module\":" << JsonString(module) << ","
            << "\"level\":" << JsonString(level) << ","
            << "\"alarm_type\":" << JsonString(alarm_type) << ","
            << "\"message\":" << JsonString(message) << ","
            << "\"result\":" << result_json
            << "}";
    Enqueue({"POST", "/api/alarm", payload.str()});
}

std::string CloudUploader::JsonString(const std::string& value)
{
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u";
                    const char* hex = "0123456789abcdef";
                    out << "00" << hex[(ch >> 4) & 0xF] << hex[ch & 0xF];
                } else {
                    out << static_cast<char>(ch);
                }
                break;
        }
    }
    out << '"';
    return out.str();
}

std::string CloudUploader::NowUtcIso()
{
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

void CloudUploader::WorkerLoop()
{
    Enqueue({"GET", "/health", ""});
    auto next_heartbeat = std::chrono::steady_clock::now();

    while (running_.load()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_heartbeat) {
            PostHeartbeat();
            next_heartbeat = now + std::chrono::seconds(std::max(1, config_.heartbeat_interval_sec));
        }

        UploadTask task;
        bool has_task = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (queue_.empty()) {
                cv_.wait_for(lock, std::chrono::milliseconds(200));
            }
            if (!queue_.empty()) {
                task = queue_.front();
                queue_.pop();
                has_task = true;
            }
        }

        if (has_task) {
            SendRequest(task);
        }
    }
}

void CloudUploader::Enqueue(const UploadTask& task)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() > 200) {
            queue_.pop();
        }
        queue_.push(task);
    }
    cv_.notify_one();
}

void CloudUploader::PostHeartbeat()
{
    bool chip_online = true;
    bool fatigue_online = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        chip_online = config_.chip_camera_online;
        fatigue_online = config_.fatigue_camera_online;
    }

    std::ostringstream payload;
    payload << "{"
            << "\"device_id\":" << JsonString(config_.device_id) << ","
            << "\"timestamp\":" << JsonString(NowUtcIso()) << ","
            << "\"status\":\"online\","
            << "\"ip\":" << JsonString(config_.rk3588_ip) << ","
            << "\"program\":" << JsonString(config_.program) << ","
            << "\"version\":" << JsonString(config_.version) << ","
            << "\"camera_status\":{"
            << "\"chip_camera\":" << JsonString(config_.chip_camera) << ","
            << "\"fatigue_camera\":" << JsonString(config_.fatigue_camera) << ","
            << "\"chip_camera_online\":" << (chip_online ? "true" : "false") << ","
            << "\"fatigue_camera_online\":" << (fatigue_online ? "true" : "false")
            << "}}";
    Enqueue({"POST", "/api/device/heartbeat", payload.str()});
}

bool CloudUploader::SendRequest(const UploadTask& task)
{
    ParsedUrl url;
    if (!parseUrl(config_.server_url, url)) {
        std::cerr << "[WARN] cloud upload invalid server_url=" << config_.server_url << "\n";
        return false;
    }

    const int sock = connectWithTimeout(url.host, url.port, std::max(1, config_.request_timeout_sec));
    if (sock < 0) {
        std::cerr << "[WARN] cloud upload connect failed server_url=" << config_.server_url
                  << " path=" << task.path << "\n";
        return false;
    }

    timeval tv{};
    tv.tv_sec = std::max(1, config_.request_timeout_sec);
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    const std::string full_path = url.base_path + task.path;
    std::ostringstream req;
    req << task.method << " " << (full_path.empty() ? "/" : full_path) << " HTTP/1.1\r\n"
        << "Host: " << url.host << ":" << url.port << "\r\n"
        << "Accept: application/json\r\n"
        << "Connection: close\r\n";
    if (!task.payload.empty()) {
        req << "Content-Type: application/json; charset=utf-8\r\n"
            << "Content-Length: " << task.payload.size() << "\r\n";
    }
    req << "\r\n";
    if (!task.payload.empty()) {
        req << task.payload;
    }

    const bool sent = sendAll(sock, req.str());
    char buffer[512];
    std::string response;
    if (sent) {
        ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            response.assign(buffer, static_cast<size_t>(n));
        }
    }
    close(sock);

    const bool ok = sent && response.find(" 2") != std::string::npos;
    std::cerr << (ok ? "[INFO] cloud upload ok " : "[WARN] cloud upload failed ")
              << "method=" << task.method
              << " server_url=" << config_.server_url
              << " device_id=" << config_.device_id
              << " path=" << task.path << "\n";
    return ok;
}
