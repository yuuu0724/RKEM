#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

struct Detection {
    cv::Rect box;
    std::string label;
    double score;
};

static int clamp_int(int value, int low, int high) {
    return std::max(low, std::min(value, high));
}

static cv::Rect make_box(int x, int y, int width, int height, const cv::Size& frame_size) {
    x = clamp_int(x, 0, std::max(0, frame_size.width - 1));
    y = clamp_int(y, 0, std::max(0, frame_size.height - 1));
    width = clamp_int(width, 1, frame_size.width - x);
    height = clamp_int(height, 1, frame_size.height - y);
    return cv::Rect(x, y, width, height);
}

static std::vector<Detection> simulate_model_a(const cv::Mat& frame, int frame_id) {
    const int w = frame.cols;
    const int h = frame.rows;
    const int box_w = std::max(80, w / 4);
    const int box_h = std::max(60, h / 4);
    const int travel = std::max(1, w - box_w - 20);
    const int x = 10 + (frame_id * 5 % travel);
    const int y = std::max(10, h / 5);

    return {
        {make_box(x, y, box_w, box_h, frame.size()), "model_a_object", 0.91},
        {make_box(w / 2, h / 2, std::max(60, w / 6), std::max(50, h / 6), frame.size()), "model_a_roi", 0.76},
    };
}

static std::vector<Detection> simulate_model_b(const cv::Mat& frame, int frame_id) {
    const int w = frame.cols;
    const int h = frame.rows;
    const int box_w = std::max(70, w / 5);
    const int box_h = std::max(70, h / 3);
    const int travel = std::max(1, h - box_h - 20);
    const int x = std::max(10, w / 3);
    const int y = 10 + (frame_id * 3 % travel);

    return {
        {make_box(x, y, box_w, box_h, frame.size()), "model_b_target", 0.88},
        {make_box(w - box_w - 20, h / 3, box_w, std::max(50, h / 5), frame.size()), "model_b_alarm", 0.69},
    };
}

static void draw_results(cv::Mat& image,
                         const std::string& title,
                         const std::vector<Detection>& detections,
                         const cv::Scalar& color,
                         double fps) {
    cv::putText(image, title, cv::Point(12, 30), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                color, 2, cv::LINE_AA);
    cv::putText(image, cv::format("FPS %.1f", fps), cv::Point(12, 62),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);

    for (const Detection& det : detections) {
        cv::rectangle(image, det.box, color, 2);
        const std::string text = cv::format("%s %.0f%%", det.label.c_str(), det.score * 100.0);
        int baseline = 0;
        const cv::Size text_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.6, 1, &baseline);
        const int label_y = std::max(det.box.y, text_size.height + 8);
        cv::Rect label_bg(det.box.x, label_y - text_size.height - 8,
                          std::min(text_size.width + 8, image.cols - det.box.x),
                          text_size.height + baseline + 8);
        cv::rectangle(image, label_bg, color, cv::FILLED);
        cv::putText(image, text, cv::Point(det.box.x + 4, label_y - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
    }
}

static std::string normalize_device_name(const std::string& device) {
    if (device.rfind("/dev/video", 0) == 0) {
        return device;
    }
    if (device.rfind("video", 0) == 0) {
        return "/dev/" + device;
    }
    return device;
}

static cv::VideoCapture open_camera(const std::string& device, int width, int height) {
    cv::VideoCapture cap;
    const std::string normalized_device = normalize_device_name(device);

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
    const std::string device = argc > 1 ? normalize_device_name(argv[1]) : "/dev/video21";
    const int width = argc > 2 ? std::stoi(argv[2]) : 640;
    const int height = argc > 3 ? std::stoi(argv[3]) : 480;

    if (argc > 4) {
        std::printf("Usage: %s [camera_device] [width] [height]\n", argv[0]);
        std::printf("Example: %s /dev/video21 640 480\n", argv[0]);
        return 1;
    }

    cv::VideoCapture cap = open_camera(device, width, height);
    if (!cap.isOpened()) {
        std::fprintf(stderr, "Failed to open camera: %s\n", device.c_str());
        return 1;
    }

    std::printf("Camera opened: %s, press q or ESC to quit.\n", device.c_str());
    cv::namedWindow("model_a_result", cv::WINDOW_NORMAL);
    cv::namedWindow("model_b_result", cv::WINDOW_NORMAL);

    int frame_id = 0;
    auto last_time = std::chrono::steady_clock::now();
    double fps = 0.0;

    while (true) {
        cv::Mat frame;
        if (!cap.read(frame) || frame.empty()) {
            std::fprintf(stderr, "Failed to read frame from camera.\n");
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last_time).count();
        if (dt > 0.0) {
            fps = 0.9 * fps + 0.1 * (1.0 / dt);
        }
        last_time = now;

        cv::Mat model_a_view = frame.clone();
        cv::Mat model_b_view = frame.clone();

        const std::vector<Detection> model_a_results = simulate_model_a(frame, frame_id);
        const std::vector<Detection> model_b_results = simulate_model_b(frame, frame_id);

        draw_results(model_a_view, "Camera -> Model A", model_a_results, cv::Scalar(0, 255, 0), fps);
        draw_results(model_b_view, "Camera -> Model B", model_b_results, cv::Scalar(0, 180, 255), fps);

        cv::imshow("model_a_result", model_a_view);
        cv::imshow("model_b_result", model_b_view);

        const int key = cv::waitKey(1);
        if (key == 'q' || key == 'Q' || key == 27) {
            break;
        }

        ++frame_id;
    }

    cap.release();
    cv::destroyAllWindows();
    return 0;
}
