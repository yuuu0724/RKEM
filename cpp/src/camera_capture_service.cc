#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

static std::atomic<bool> g_running(true);

static void SignalHandler(int) {
    g_running.store(false);
}

struct RawFrameHeader {
    char magic[8];
    int width;
    int height;
    int type;
    int bytes;
};

static std::string NormalizeDeviceName(const std::string& device) {
    if (device.rfind("/dev/video", 0) == 0) {
        return device;
    }
    if (device.rfind("video", 0) == 0) {
        return "/dev/" + device;
    }
    return device;
}

static cv::VideoCapture OpenCamera(const std::string& device, int width, int height) {
    cv::VideoCapture cap;
    const std::string normalized_device = NormalizeDeviceName(device);

    if (normalized_device.rfind("/dev/video", 0) == 0) {
        cap.open(normalized_device, cv::CAP_V4L2);
        if (!cap.isOpened()) {
            const int id = std::stoi(normalized_device.substr(10));
            cap.open(id, cv::CAP_V4L2);
        }
    } else {
        cap.open(std::stoi(normalized_device), cv::CAP_V4L2);
    }

    if (cap.isOpened()) {
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
        cap.set(cv::CAP_PROP_FPS, 30);
        cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    }

    return cap;
}

int main(int argc, char** argv) {
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    std::string device = "/dev/video21";
    std::string output = "/tmp/integrated_inspection_video21.bgr";
    int width = 640;
    int height = 480;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--camera" && i + 1 < argc) {
            device = NormalizeDeviceName(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "--width" && i + 1 < argc) {
            width = std::stoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            height = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::printf("Usage: %s [--camera /dev/video21] [--output /tmp/frame.bgr] [--width 640] [--height 480]\n", argv[0]);
            return 0;
        }
    }

    cv::VideoCapture cap = OpenCamera(device, width, height);
    if (!cap.isOpened()) {
        std::fprintf(stderr, "[ERROR] camera_capture_service failed to open %s\n", device.c_str());
        return 1;
    }

    std::printf("[INFO] camera_capture_service opened %s actual=%.0fx%.0f fps=%.1f output=%s\n",
                device.c_str(),
                cap.get(cv::CAP_PROP_FRAME_WIDTH),
                cap.get(cv::CAP_PROP_FRAME_HEIGHT),
                cap.get(cv::CAP_PROP_FPS),
                output.c_str());
    std::fflush(stdout);

    const std::string tmp_output = output + ".tmp";
    bool printed_frame_info = false;
    int empty_count = 0;

    while (g_running.load()) {
        cv::Mat frame;
        cap >> frame;
        if (frame.empty()) {
            empty_count++;
            if (empty_count == 1 || empty_count % 200 == 0) {
                std::fprintf(stderr, "[WARN] camera_capture_service read empty frame count=%d\n", empty_count);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        empty_count = 0;

        if (!printed_frame_info) {
            std::printf("[INFO] camera_capture_service first frame: %dx%d channels=%d type=%d\n",
                        frame.cols, frame.rows, frame.channels(), frame.type());
            std::fflush(stdout);
            printed_frame_info = true;
        }

        cv::Mat continuous = frame.isContinuous() ? frame : frame.clone();
        RawFrameHeader header = {{'I', 'I', 'F', 'R', 'M', '0', '1', '\0'},
                                 continuous.cols,
                                 continuous.rows,
                                 continuous.type(),
                                 static_cast<int>(continuous.total() * continuous.elemSize())};
        std::ofstream out(tmp_output, std::ios::binary | std::ios::trunc);
        if (out.write(reinterpret_cast<const char*>(&header), sizeof(header)) &&
            out.write(reinterpret_cast<const char*>(continuous.data), header.bytes)) {
            out.close();
            std::rename(tmp_output.c_str(), output.c_str());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    cap.release();
    return 0;
}
