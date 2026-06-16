#include "../../../chip/cpp/ppocrv5.h"
#include "audio_alert.h"
#include "cloud_uploader.h"
#include "serial_port.h"

#include <rknn_api.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>
#include <sys/wait.h>
#include <unistd.h>

static constexpr int kInputSize = 640;
static constexpr int kCaptureWidth = 640;
static constexpr int kCaptureHeight = 480;
static constexpr float kNmsThreshold = 0.45f;
static constexpr float kDefectThreshold = 0.15f;
static constexpr float kFatigueThreshold = 0.15f;
static const char* kChipCameraDevice = "/dev/video21";
static const char* kChipFramePath = "/tmp/integrated_inspection_video21.bgr";
static constexpr uint64_t kOcrIntervalFrames = 3;
static const char* kDefaultSerialPort = "/dev/ttyS9";
static const char* kDefaultArrivalCommand = "DONE";
static const char* kDefaultMoveCommand = "MOVE_NEXT\n";
static const char* kDefaultCloudUploadConfig = "config/cloud_upload.ini";
static constexpr int kDefaultChipSlots = 4;

static std::atomic<bool> g_running(true);
static std::atomic<uint64_t> g_chip_arrival_requests(0);

struct FrameStore {
    std::mutex mutex;
    cv::Mat gray640;
    cv::Mat display;
    uint64_t seq = 0;
    bool ready = false;
};

struct OcrState {
    bool ready = false;
    bool model_ok = false;
    std::string raw_text;
    std::string filtered_text;
    ppocr_text_recog_array_result_t results;
};

struct Detection {
    int cls_id = -1;
    float score = 0.0f;
    cv::Rect box;
};

struct DetectionState {
    bool ready = false;
    std::vector<Detection> detections;
};

struct RawFrameHeader {
    char magic[8];
    int width;
    int height;
    int type;
    int bytes;
};

static FrameStore g_chip_frame;
static FrameStore g_fatigue_frame;
static std::mutex g_ocr_mutex;
static std::mutex g_defect_mutex;
static std::mutex g_fatigue_mutex;
static OcrState g_ocr_state;
static DetectionState g_defect_state;
static DetectionState g_fatigue_state;

static void SignalHandler(int) {
    g_running.store(false);
}

static void DrawWindowTitle(cv::Mat& image, const std::string& title);

static void ConfigureDisplayEnv() {
    if (!getenv("DISPLAY")) {
        setenv("DISPLAY", ":0", 0);
    }
    if (!getenv("XDG_RUNTIME_DIR")) {
        std::string runtime_dir = "/run/user/" + std::to_string(getuid());
        setenv("XDG_RUNTIME_DIR", runtime_dir.c_str(), 0);
    }
}

static std::string TrimDeviceName(const std::string& device) {
    const std::string prefix = "/dev/";
    if (device.rfind(prefix, 0) == 0) {
        return device.substr(prefix.size());
    }
    return device;
}

static bool ParseVideoIndex(const std::string& device, int& index) {
    const std::string prefix = "/dev/video";
    if (device.rfind(prefix, 0) != 0) {
        return false;
    }
    std::string value = device.substr(prefix.size());
    if (value.empty()) {
        return false;
    }
    for (char ch : value) {
        if (!std::isdigit((unsigned char)ch)) {
            return false;
        }
    }
    index = std::stoi(value);
    return true;
}

static std::string FourccToString(double fourcc_value) {
    int fourcc = static_cast<int>(fourcc_value);
    std::string value(4, ' ');
    value[0] = static_cast<char>(fourcc & 0xff);
    value[1] = static_cast<char>((fourcc >> 8) & 0xff);
    value[2] = static_cast<char>((fourcc >> 16) & 0xff);
    value[3] = static_cast<char>((fourcc >> 24) & 0xff);
    for (char& ch : value) {
        if (!std::isprint((unsigned char)ch)) {
            ch = '?';
        }
    }
    return value;
}

static bool UseYuyvCamera(const std::string& device) {
    return device == "/dev/video23" || device == "23";
}

static std::string NormalizeText(const std::string& text) {
    std::string out;
    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            out.push_back((char)std::toupper(ch));
        }
    }
    return out;
}

static int LcsLength(const std::string& a, const std::string& b) {
    std::vector<int> dp(b.size() + 1, 0);
    for (char ca : a) {
        int prev = 0;
        for (size_t j = 1; j <= b.size(); ++j) {
            int saved = dp[j];
            if (ca == b[j - 1]) {
                dp[j] = prev + 1;
            } else {
                dp[j] = std::max(dp[j], dp[j - 1]);
            }
            prev = saved;
        }
    }
    return dp[b.size()];
}

static bool MatchModelName(const std::string& expected, const std::string& observed) {
    if (expected.empty() || observed.empty()) {
        return false;
    }
    if (observed.find(expected) != std::string::npos) {
        return true;
    }
    int lcs = LcsLength(expected, observed);
    int allowed_missing = expected.size() <= 4 ? 0 : 1;
    return (int)expected.size() - lcs <= allowed_missing;
}

static std::string TrimAscii(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace((unsigned char)text[begin])) {
        begin++;
    }
    size_t end = text.size();
    while (end > begin && std::isspace((unsigned char)text[end - 1])) {
        end--;
    }
    return text.substr(begin, end - begin);
}

static std::string DecodeEscapedCommand(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out.push_back(text[i]);
            continue;
        }
        char next = text[++i];
        switch (next) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case '\\': out.push_back('\\'); break;
            default:
                out.push_back('\\');
                out.push_back(next);
                break;
        }
    }
    return out;
}

static void ReplaceAll(std::string& value, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }
    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static std::string FormatMotorCommand(const std::string& command_template,
                                      int current_slot,
                                      int next_slot) {
    std::string command = command_template;
    ReplaceAll(command, "{current}", std::to_string(current_slot));
    ReplaceAll(command, "{next}", std::to_string(next_slot));
    return command;
}

static std::string PrintableSerialText(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char ch : text) {
        if (ch == '\n') {
            out += "\\n";
        } else if (ch == '\r') {
            out += "\\r";
        } else if (std::isprint(ch)) {
            out.push_back((char)ch);
        } else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02X", ch);
            out += buf;
        }
    }
    return out;
}

static std::vector<std::string> LoadLabels(const std::string& path) {
    std::vector<std::string> labels;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            labels.push_back(line);
        }
    }
    return labels;
}

static unsigned char* LoadModel(const std::string& path, int* model_size) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) {
        std::fprintf(stderr, "[ERROR] fopen %s failed\n", path.c_str());
        return nullptr;
    }
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char* data = (unsigned char*)std::malloc(size);
    if (!data) {
        fclose(fp);
        return nullptr;
    }
    if ((int)fread(data, 1, size, fp) != size) {
        std::free(data);
        fclose(fp);
        return nullptr;
    }
    fclose(fp);
    *model_size = size;
    return data;
}

static float Sigmoid(float x) {
    if (x >= 0.0f) {
        float z = std::exp(-x);
        return 1.0f / (1.0f + z);
    }
    float z = std::exp(x);
    return z / (1.0f + z);
}

static float DequantI8(int8_t q, int32_t zp, float scale) {
    return ((float)q - (float)zp) * scale;
}

static float DequantU8(uint8_t q, int32_t zp, float scale) {
    return ((float)q - (float)zp) * scale;
}

class YoloDetector {
public:
    bool Init(const std::string& model_path, const std::string& labels_path,
              int class_count, float threshold, rknn_core_mask core_mask) {
        class_count_ = class_count;
        threshold_ = threshold;
        labels_ = LoadLabels(labels_path);
        int model_size = 0;
        unsigned char* model = LoadModel(model_path, &model_size);
        if (!model) {
            return false;
        }
        int ret = rknn_init(&ctx_, model, model_size, 0, nullptr);
        std::free(model);
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "[ERROR] rknn_init %s failed: %d\n", model_path.c_str(), ret);
            return false;
        }
        ret = rknn_set_core_mask(ctx_, core_mask);
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "[WARN] set core mask failed for %s: %d\n", model_path.c_str(), ret);
        }

        rknn_input_output_num io_num;
        ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
        if (ret != RKNN_SUCC) {
            return false;
        }
        input_attrs_.resize(io_num.n_input);
        output_attrs_.resize(io_num.n_output);
        for (uint32_t i = 0; i < io_num.n_input; ++i) {
            memset(&input_attrs_[i], 0, sizeof(rknn_tensor_attr));
            input_attrs_[i].index = i;
            rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attrs_[i], sizeof(rknn_tensor_attr));
        }
        for (uint32_t i = 0; i < io_num.n_output; ++i) {
            memset(&output_attrs_[i], 0, sizeof(rknn_tensor_attr));
            output_attrs_[i].index = i;
            rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[i], sizeof(rknn_tensor_attr));
        }
        if (input_attrs_[0].fmt == RKNN_TENSOR_NCHW) {
            model_channel_ = input_attrs_[0].dims[1];
            model_height_ = input_attrs_[0].dims[2];
            model_width_ = input_attrs_[0].dims[3];
        } else {
            model_height_ = input_attrs_[0].dims[1];
            model_width_ = input_attrs_[0].dims[2];
            model_channel_ = input_attrs_[0].dims[3];
        }
        std::printf("[INFO] YOLO model loaded: %s input=%dx%dx%d outputs=%zu\n",
                    model_path.c_str(), model_width_, model_height_, model_channel_, output_attrs_.size());
        for (size_t i = 0; i < output_attrs_.size(); ++i) {
            const auto& attr = output_attrs_[i];
            std::printf("[INFO]   output%zu dims=[%d,%d,%d,%d] n_dims=%d n_elems=%d type=%s scale=%f zp=%d\n",
                        i, attr.dims[0], attr.dims[1], attr.dims[2], attr.dims[3],
                        attr.n_dims, attr.n_elems, get_type_string(attr.type), attr.scale, attr.zp);
        }
        return true;
    }

    void Release() {
        if (ctx_) {
            rknn_destroy(ctx_);
            ctx_ = 0;
        }
    }

    std::string Label(int cls_id) const {
        if (cls_id >= 0 && cls_id < (int)labels_.size()) {
            return labels_[cls_id];
        }
        return "class_" + std::to_string(cls_id);
    }

    bool Detect(const cv::Mat& gray640, std::vector<Detection>& detections) {
        if (!ctx_ || gray640.empty()) {
            return false;
        }

        cv::Mat resized;
        if (gray640.cols != model_width_ || gray640.rows != model_height_) {
            cv::resize(gray640, resized, cv::Size(model_width_, model_height_));
        } else {
            resized = gray640;
        }

        cv::Mat input;
        if (model_channel_ == 1) {
            input = resized.isContinuous() ? resized : resized.clone();
        } else {
            cv::cvtColor(resized, input, cv::COLOR_GRAY2RGB);
        }

        rknn_input inputs[1];
        memset(inputs, 0, sizeof(inputs));
        inputs[0].index = 0;
        inputs[0].type = RKNN_TENSOR_UINT8;
        inputs[0].fmt = RKNN_TENSOR_NHWC;
        inputs[0].size = model_width_ * model_height_ * model_channel_;
        inputs[0].buf = input.data;

        int ret = rknn_inputs_set(ctx_, 1, inputs);
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "[ERROR] rknn_inputs_set failed: %d\n", ret);
            return false;
        }
        ret = rknn_run(ctx_, nullptr);
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "[ERROR] rknn_run failed: %d\n", ret);
            return false;
        }

        std::vector<rknn_output> outputs(output_attrs_.size());
        memset(outputs.data(), 0, outputs.size() * sizeof(rknn_output));
        for (size_t i = 0; i < outputs.size(); ++i) {
            outputs[i].index = i;
            outputs[i].want_float = IsQuantTensor(output_attrs_[i]) ? 0 : 1;
        }
        ret = rknn_outputs_get(ctx_, outputs.size(), outputs.data(), nullptr);
        if (ret != RKNN_SUCC) {
            std::fprintf(stderr, "[ERROR] rknn_outputs_get failed: %d\n", ret);
            return false;
        }

        detections.clear();
        if (outputs.size() == 2 && outputs[0].buf && outputs[1].buf) {
            DecodeTwoOutputs(outputs, detections);
        } else if (!outputs.empty() && outputs[0].buf) {
            DecodeSingleOutput(outputs[0].buf, output_attrs_[0], detections);
        } else {
            std::fprintf(stderr, "[ERROR] %s has no valid output buffers\n", Label(0).c_str());
        }
        rknn_outputs_release(ctx_, outputs.size(), outputs.data());
        return true;
    }

private:
    struct Layout {
        bool channel_first = true;
        int channels = 0;
        int anchors = 0;
        bool has_objectness = false;
    };

    struct Candidate {
        float box[4];
        float score = 0.0f;
        int cls_id = -1;
    };

    static bool IsQuantTensor(const rknn_tensor_attr& attr) {
        return attr.type == RKNN_TENSOR_INT8 || attr.type == RKNN_TENSOR_UINT8;
    }

    float Value(void* output, const rknn_tensor_attr& attr, int offset) const {
        if (attr.type == RKNN_TENSOR_INT8) {
            return DequantI8(((int8_t*)output)[offset], attr.zp, attr.scale);
        }
        if (attr.type == RKNN_TENSOR_UINT8) {
            return DequantU8(((uint8_t*)output)[offset], attr.zp, attr.scale);
        }
        return ((float*)output)[offset];
    }

    bool GetLayout(const rknn_tensor_attr& attr, Layout& layout) const {
        int no_obj = class_count_ + 4;
        int with_obj = class_count_ + 5;
        if (attr.n_dims == 3) {
            if (attr.dims[1] == no_obj || attr.dims[1] == with_obj) {
                layout.channel_first = true;
                layout.channels = attr.dims[1];
                layout.anchors = attr.n_elems / layout.channels;
                layout.has_objectness = layout.channels == with_obj;
                return true;
            }
            if (attr.dims[2] == no_obj || attr.dims[2] == with_obj) {
                layout.channel_first = false;
                layout.channels = attr.dims[2];
                layout.anchors = attr.n_elems / layout.channels;
                layout.has_objectness = layout.channels == with_obj;
                return true;
            }
        }
        if (attr.n_dims == 2) {
            if (attr.dims[0] == no_obj || attr.dims[0] == with_obj) {
                layout.channel_first = true;
                layout.channels = attr.dims[0];
                layout.anchors = attr.dims[1];
                layout.has_objectness = layout.channels == with_obj;
                return true;
            }
            if (attr.dims[1] == no_obj || attr.dims[1] == with_obj) {
                layout.channel_first = false;
                layout.channels = attr.dims[1];
                layout.anchors = attr.dims[0];
                layout.has_objectness = layout.channels == with_obj;
                return true;
            }
        }
        std::fprintf(stderr, "[ERROR] Unsupported YOLO output shape dims=[%d,%d,%d,%d], n_dims=%d\n",
                     attr.dims[0], attr.dims[1], attr.dims[2], attr.dims[3], attr.n_dims);
        return false;
    }

    int Offset(const Layout& layout, int anchor, int channel) const {
        if (layout.channel_first) {
            return channel * layout.anchors + anchor;
        }
        return anchor * layout.channels + channel;
    }

    static void RawBoxToXyxy(const Candidate& c, bool xywh, bool normalized,
                             int image_size, float xyxy[4]) {
        float b[4] = {c.box[0], c.box[1], c.box[2], c.box[3]};
        if (normalized) {
            for (float& v : b) {
                v *= (float)image_size;
            }
        }
        if (xywh) {
            xyxy[0] = b[0] - b[2] / 2.0f;
            xyxy[1] = b[1] - b[3] / 2.0f;
            xyxy[2] = b[0] + b[2] / 2.0f;
            xyxy[3] = b[1] + b[3] / 2.0f;
        } else {
            xyxy[0] = b[0];
            xyxy[1] = b[1];
            xyxy[2] = b[2];
            xyxy[3] = b[3];
        }
    }

    static int BoxQuality(const std::vector<Candidate>& candidates, bool xywh,
                          bool normalized, int image_size, float& median_area) {
        std::vector<float> areas;
        for (const auto& c : candidates) {
            float b[4];
            RawBoxToXyxy(c, xywh, normalized, image_size, b);
            float w = b[2] - b[0];
            float h = b[3] - b[1];
            float area = w * h;
            bool intersects = b[2] > 0.0f && b[3] > 0.0f && b[0] < image_size && b[1] < image_size;
            if (intersects && w > 2.0f && h > 2.0f && area > 8.0f && area < image_size * image_size * 0.95f) {
                areas.push_back(area);
            }
        }
        if (areas.empty()) {
            median_area = 0.0f;
            return 0;
        }
        std::sort(areas.begin(), areas.end());
        median_area = areas[areas.size() / 2];
        return areas.size();
    }

    static bool ChooseXywh(const std::vector<Candidate>& candidates, bool normalized, int image_size) {
        float xywh_area = 0.0f;
        float xyxy_area = 0.0f;
        int xywh_score = BoxQuality(candidates, true, normalized, image_size, xywh_area);
        int xyxy_score = BoxQuality(candidates, false, normalized, image_size, xyxy_area);
        if (xyxy_score > xywh_score) {
            return false;
        }
        if (xywh_score > xyxy_score) {
            return true;
        }
        if (xywh_area <= 0.0f || xyxy_area <= 0.0f) {
            return true;
        }
        return !(xyxy_area < xywh_area * 0.35f);
    }

    static float Iou(const cv::Rect& a, const cv::Rect& b) {
        int inter = (a & b).area();
        int uni = a.area() + b.area() - inter;
        return uni <= 0 ? 0.0f : (float)inter / (float)uni;
    }

    void DecodeSingleOutput(void* output, const rknn_tensor_attr& attr,
                            std::vector<Detection>& detections) {
        Layout layout;
        if (!GetLayout(attr, layout)) {
            return;
        }

        float raw_min = 1.0e30f;
        float raw_max = -1.0e30f;
        float obj_min = 1.0e30f;
        float obj_max = -1.0e30f;
        for (int i = 0; i < layout.anchors; ++i) {
            if (layout.has_objectness) {
                float obj = Value(output, attr, Offset(layout, i, 4));
                obj_min = std::min(obj_min, obj);
                obj_max = std::max(obj_max, obj);
            }
            for (int c = 0; c < class_count_; ++c) {
                int score_ch = layout.has_objectness ? 5 + c : 4 + c;
                float raw = Value(output, attr, Offset(layout, i, score_ch));
                raw_min = std::min(raw_min, raw);
                raw_max = std::max(raw_max, raw);
            }
        }
        bool sigmoid_score = raw_min < 0.0f || raw_max > 1.0f;
        bool sigmoid_obj = layout.has_objectness && (obj_min < 0.0f || obj_max > 1.0f);

        std::vector<Candidate> candidates;
        for (int i = 0; i < layout.anchors; ++i) {
            float best_score = 0.0f;
            int best_cls = -1;
            for (int c = 0; c < class_count_; ++c) {
                int score_ch = layout.has_objectness ? 5 + c : 4 + c;
                float raw = Value(output, attr, Offset(layout, i, score_ch));
                float score = sigmoid_score ? Sigmoid(raw) : std::max(0.0f, std::min(raw, 1.0f));
                if (score > threshold_ && score > best_score) {
                    best_score = score;
                    best_cls = c;
                }
            }
            if (best_cls < 0) {
                continue;
            }
            if (layout.has_objectness) {
                float obj = Value(output, attr, Offset(layout, i, 4));
                best_score *= sigmoid_obj ? Sigmoid(obj) : std::max(0.0f, std::min(obj, 1.0f));
            }
            if (best_score < threshold_) {
                continue;
            }

            Candidate cand;
            for (int k = 0; k < 4; ++k) {
                cand.box[k] = Value(output, attr, Offset(layout, i, k));
            }
            cand.score = best_score;
            cand.cls_id = best_cls;
            candidates.push_back(cand);
        }
        if (candidates.empty()) {
            return;
        }

        float max_abs = 0.0f;
        for (const auto& c : candidates) {
            for (float v : c.box) {
                max_abs = std::max(max_abs, std::fabs(v));
            }
        }
        bool normalized = max_abs <= 2.0f;
        bool xywh = ChooseXywh(candidates, normalized, model_width_);

        std::vector<Detection> decoded;
        for (const auto& c : candidates) {
            float b[4];
            RawBoxToXyxy(c, xywh, normalized, model_width_, b);
            int x1 = std::max(0, std::min((int)std::round(b[0]), model_width_ - 1));
            int y1 = std::max(0, std::min((int)std::round(b[1]), model_height_ - 1));
            int x2 = std::max(0, std::min((int)std::round(b[2]), model_width_ - 1));
            int y2 = std::max(0, std::min((int)std::round(b[3]), model_height_ - 1));
            if (x2 - x1 <= 2 || y2 - y1 <= 2) {
                continue;
            }
            decoded.push_back({c.cls_id, c.score, cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2))});
        }

        std::sort(decoded.begin(), decoded.end(), [](const Detection& a, const Detection& b) {
            return a.score > b.score;
        });
        std::vector<bool> removed(decoded.size(), false);
        for (size_t i = 0; i < decoded.size(); ++i) {
            if (removed[i]) {
                continue;
            }
            detections.push_back(decoded[i]);
            for (size_t j = i + 1; j < decoded.size(); ++j) {
                if (!removed[j] && decoded[i].cls_id == decoded[j].cls_id &&
                    Iou(decoded[i].box, decoded[j].box) > kNmsThreshold) {
                    removed[j] = true;
                }
            }
        }
    }

    bool MatchOutputShape(const rknn_tensor_attr& attr, int channels, bool& channel_first, int& anchors) const {
        if (attr.n_dims == 3) {
            if (attr.dims[1] == channels) {
                channel_first = true;
                anchors = attr.n_elems / channels;
                return anchors > 0;
            }
            if (attr.dims[2] == channels) {
                channel_first = false;
                anchors = attr.n_elems / channels;
                return anchors > 0;
            }
        }
        if (attr.n_dims == 2) {
            if (attr.dims[0] == channels) {
                channel_first = true;
                anchors = attr.dims[1];
                return anchors > 0;
            }
            if (attr.dims[1] == channels) {
                channel_first = false;
                anchors = attr.dims[0];
                return anchors > 0;
            }
        }
        return false;
    }

    int TwoOutputOffset(bool channel_first, int anchors, int channels, int anchor, int channel) const {
        return channel_first ? channel * anchors + anchor : anchor * channels + channel;
    }

    void DecodeTwoOutputs(const std::vector<rknn_output>& outputs, std::vector<Detection>& detections) {
        int box_idx = -1;
        int score_idx = -1;
        int box_anchors = 0;
        int score_anchors = 0;
        bool box_channel_first = true;
        bool score_channel_first = true;

        for (size_t i = 0; i < output_attrs_.size(); ++i) {
            int anchors = 0;
            bool channel_first = true;
            if (MatchOutputShape(output_attrs_[i], 4, channel_first, anchors)) {
                box_idx = (int)i;
                box_anchors = anchors;
                box_channel_first = channel_first;
            }
            if (MatchOutputShape(output_attrs_[i], class_count_, channel_first, anchors)) {
                score_idx = (int)i;
                score_anchors = anchors;
                score_channel_first = channel_first;
            }
        }

        if (box_idx < 0 || score_idx < 0 || box_anchors <= 0 || box_anchors != score_anchors) {
            std::fprintf(stderr, "[ERROR] unsupported two-output YOLO shape\n");
            return;
        }

        void* box_output = outputs[box_idx].buf;
        void* score_output = outputs[score_idx].buf;
        const auto& box_attr = output_attrs_[box_idx];
        const auto& score_attr = output_attrs_[score_idx];
        int anchors = box_anchors;

        float raw_min = 1.0e30f;
        float raw_max = -1.0e30f;
        for (int i = 0; i < anchors; ++i) {
            for (int c = 0; c < class_count_; ++c) {
                int off = TwoOutputOffset(score_channel_first, anchors, class_count_, i, c);
                float raw = Value(score_output, score_attr, off);
                raw_min = std::min(raw_min, raw);
                raw_max = std::max(raw_max, raw);
            }
        }
        bool sigmoid_score = raw_min < 0.0f || raw_max > 1.0f;

        std::vector<Detection> decoded;
        for (int i = 0; i < anchors; ++i) {
            float best_score = 0.0f;
            int best_cls = -1;
            for (int c = 0; c < class_count_; ++c) {
                int off = TwoOutputOffset(score_channel_first, anchors, class_count_, i, c);
                float raw = Value(score_output, score_attr, off);
                float score = sigmoid_score ? Sigmoid(raw) : std::max(0.0f, std::min(raw, 1.0f));
                if (score > threshold_ && score > best_score) {
                    best_score = score;
                    best_cls = c;
                }
            }
            if (best_cls < 0) {
                continue;
            }

            float cx = Value(box_output, box_attr, TwoOutputOffset(box_channel_first, anchors, 4, i, 0));
            float cy = Value(box_output, box_attr, TwoOutputOffset(box_channel_first, anchors, 4, i, 1));
            float w = Value(box_output, box_attr, TwoOutputOffset(box_channel_first, anchors, 4, i, 2));
            float h = Value(box_output, box_attr, TwoOutputOffset(box_channel_first, anchors, 4, i, 3));
            if (std::max(std::max(std::fabs(cx), std::fabs(cy)), std::max(std::fabs(w), std::fabs(h))) <= 2.0f) {
                cx *= model_width_;
                w *= model_width_;
                cy *= model_height_;
                h *= model_height_;
            }

            int x1 = std::max(0, std::min((int)std::round(cx - w / 2.0f), model_width_ - 1));
            int y1 = std::max(0, std::min((int)std::round(cy - h / 2.0f), model_height_ - 1));
            int x2 = std::max(0, std::min((int)std::round(cx + w / 2.0f), model_width_ - 1));
            int y2 = std::max(0, std::min((int)std::round(cy + h / 2.0f), model_height_ - 1));
            if (x2 - x1 <= 2 || y2 - y1 <= 2) {
                continue;
            }
            decoded.push_back({best_cls, best_score, cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2))});
        }

        std::sort(decoded.begin(), decoded.end(), [](const Detection& a, const Detection& b) {
            return a.score > b.score;
        });
        std::vector<bool> removed(decoded.size(), false);
        for (size_t i = 0; i < decoded.size(); ++i) {
            if (removed[i]) {
                continue;
            }
            detections.push_back(decoded[i]);
            for (size_t j = i + 1; j < decoded.size(); ++j) {
                if (!removed[j] && decoded[i].cls_id == decoded[j].cls_id &&
                    Iou(decoded[i].box, decoded[j].box) > kNmsThreshold) {
                    removed[j] = true;
                }
            }
        }
    }

    rknn_context ctx_ = 0;
    std::vector<rknn_tensor_attr> input_attrs_;
    std::vector<rknn_tensor_attr> output_attrs_;
    std::vector<std::string> labels_;
    int model_width_ = kInputSize;
    int model_height_ = kInputSize;
    int model_channel_ = 3;
    int class_count_ = 0;
    float threshold_ = 0.15f;
};

static bool OpenCamera(cv::VideoCapture& cap, const std::string& device) {
    for (int i = 0; i < 5 && g_running.load(); ++i) {
        int index = -1;
        if (device.rfind("/dev/video", 0) == 0) {
            cap.open(device, cv::CAP_V4L2);
            if (!cap.isOpened() && ParseVideoIndex(device, index)) {
                cap.open(index, cv::CAP_V4L2);
            }
        } else if (ParseVideoIndex(device, index)) {
            cap.open(index, cv::CAP_V4L2);
        }
        if (!cap.isOpened()) {
            cap.open(device, cv::CAP_V4L2);
        }
        if (cap.isOpened()) {
            int fourcc = UseYuyvCamera(device)
                ? cv::VideoWriter::fourcc('Y', 'U', 'Y', 'V')
                : cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
            cap.set(cv::CAP_PROP_FOURCC, fourcc);
            cap.set(cv::CAP_PROP_FRAME_WIDTH, kCaptureWidth);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, kCaptureHeight);
            cap.set(cv::CAP_PROP_FPS, 30);
            cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
            return true;
        }
        std::fprintf(stderr, "[WARN] open %s failed, retry %d/5\n", device.c_str(), i + 1);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
}

static std::string NormalizeVideoDevice(const std::string& device) {
    if (device.rfind("/dev/video", 0) == 0) {
        return device;
    }
    if (device.rfind("video", 0) == 0) {
        return "/dev/" + device;
    }
    return device;
}

static cv::VideoCapture OpenCameraLikeDemo(const std::string& device, int width, int height) {
    cv::VideoCapture cap;
    const std::string normalized_device = NormalizeVideoDevice(device);

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

static pid_t StartCameraCaptureService(const std::string& device, const std::string& output) {
    unlink(output.c_str());
    std::string tmp_output = output + ".tmp";
    unlink(tmp_output.c_str());

    pid_t pid = fork();
    if (pid < 0) {
        std::perror("fork camera_capture_service");
        return -1;
    }
    if (pid == 0) {
        execl("./camera_capture_service", "camera_capture_service",
              "--camera", device.c_str(),
              "--output", output.c_str(),
              "--width", "640",
              "--height", "480",
              (char*)nullptr);
        execlp("camera_capture_service", "camera_capture_service",
               "--camera", device.c_str(),
               "--output", output.c_str(),
               "--width", "640",
               "--height", "480",
               (char*)nullptr);
        std::perror("exec camera_capture_service");
        _exit(127);
    }
    return pid;
}

static void StopCameraCaptureService(pid_t pid) {
    if (pid <= 0) {
        return;
    }
    kill(pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        int status = 0;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

static bool ReadLatestChipFrame(cv::Mat& frame) {
    std::ifstream in(kChipFramePath, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    RawFrameHeader header;
    if (!in.read(reinterpret_cast<char*>(&header), sizeof(header))) {
        return false;
    }
    if (std::string(header.magic, header.magic + 7) != "IIFRM01" ||
        header.width <= 0 || header.height <= 0 || header.bytes <= 0) {
        return false;
    }

    cv::Mat image(header.height, header.width, header.type);
    const size_t expected_bytes = image.total() * image.elemSize();
    if (header.bytes != static_cast<int>(expected_bytes)) {
        return false;
    }
    if (!in.read(reinterpret_cast<char*>(image.data), expected_bytes)) {
        return false;
    }

    frame = image;
    return true;
}

static int RunDisplayOnlyFromCaptureService(const std::string& device) {
    pid_t capture_pid = StartCameraCaptureService(device, kChipFramePath);
    if (capture_pid <= 0) {
        return 1;
    }

    ConfigureDisplayEnv();
    cv::namedWindow("video21 OCR", cv::WINDOW_NORMAL);
    cv::namedWindow("video21 defect", cv::WINDOW_NORMAL);

    bool printed_frame_info = false;
    while (g_running.load()) {
        cv::Mat frame;
        if (!ReadLatestChipFrame(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        if (!printed_frame_info) {
            std::printf("[INFO] display-only first frame: %dx%d channels=%d type=%d\n",
                        frame.cols, frame.rows, frame.channels(), frame.type());
            printed_frame_info = true;
        }

        cv::imshow("video21 OCR", frame.clone());
        cv::imshow("video21 defect", frame.clone());

        int key = cv::waitKey(1);
        if (key == 27 || key == 'q' || key == 'Q') {
            break;
        }
    }

    StopCameraCaptureService(capture_pid);
    cv::destroyAllWindows();
    return 0;
}

static bool DecodeJpegFrameIfNeeded(const cv::Mat& frame, cv::Mat& bgr) {
    if (frame.empty() || frame.type() != CV_8UC1 || frame.total() < 4) {
        return false;
    }

    const unsigned char* data = frame.ptr<unsigned char>(0);
    bool looks_like_jpeg = data[0] == 0xff && data[1] == 0xd8;
    if (!looks_like_jpeg && frame.rows > 1) {
        return false;
    }

    cv::Mat encoded = frame.isContinuous() ? frame.reshape(1, 1) : frame.clone().reshape(1, 1);
    cv::Mat decoded = cv::imdecode(encoded, cv::IMREAD_COLOR);
    if (decoded.empty()) {
        return false;
    }

    bgr = decoded;
    return true;
}

static bool ConvertCameraFrameToBgr(const std::string& device, const cv::Mat& frame, cv::Mat& bgr) {
    if (frame.empty()) {
        return false;
    }

    try {
        if (frame.channels() == 1) {
            if (DecodeJpegFrameIfNeeded(frame, bgr)) {
                return true;
            }
            cv::cvtColor(frame, bgr, cv::COLOR_GRAY2BGR);
        } else if (frame.channels() == 2) {
            cv::cvtColor(frame, bgr, cv::COLOR_YUV2BGR_YUY2);
        } else if (frame.channels() == 3) {
            bgr = frame.clone();
        } else if (frame.channels() == 4) {
            cv::cvtColor(frame, bgr, cv::COLOR_BGRA2BGR);
        } else {
            std::fprintf(stderr, "[WARN] %s unsupported frame channels=%d type=%d\n",
                         device.c_str(), frame.channels(), frame.type());
            return false;
        }
    } catch (const cv::Exception& e) {
        std::fprintf(stderr, "[ERROR] %s frame convert failed: %s\n", device.c_str(), e.what());
        return false;
    }

    return true;
}

static bool PrepareChipFrame(const std::string& device, const cv::Mat& frame,
                             cv::Mat& gray640, cv::Mat& display) {
    if (frame.empty()) {
        return false;
    }

    try {
        cv::Mat bgr;
        if (!ConvertCameraFrameToBgr(device, frame, bgr)) {
            return false;
        }

        cv::Mat gray;
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
        cv::resize(gray, gray640, cv::Size(kInputSize, kInputSize));
        display = bgr.clone();
    } catch (const cv::Exception& e) {
        std::fprintf(stderr, "[ERROR] %s frame convert failed: %s\n", device.c_str(), e.what());
        return false;
    }

    return true;
}

static void CaptureThread(const std::string& device, FrameStore* store) {
    cv::VideoCapture cap;
    if (!OpenCamera(cap, device)) {
        std::fprintf(stderr, "[ERROR] failed to open camera %s\n", device.c_str());
        return;
    }
    std::printf("[INFO] camera opened: %s actual=%.0fx%.0f fourcc=%s fps=%.1f\n",
                device.c_str(), cap.get(cv::CAP_PROP_FRAME_WIDTH),
                cap.get(cv::CAP_PROP_FRAME_HEIGHT),
                FourccToString(cap.get(cv::CAP_PROP_FOURCC)).c_str(),
                cap.get(cv::CAP_PROP_FPS));

    cv::Mat frame;
    int empty_count = 0;
    bool printed_frame_info = false;
    int dark_frame_count = 0;
    while (g_running.load()) {
        cap >> frame;
        if (frame.empty()) {
            empty_count++;
            if (empty_count % 200 == 0) {
                std::fprintf(stderr, "[WARN] %s read empty frame count=%d\n", device.c_str(), empty_count);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        empty_count = 0;

        try {
            if (!printed_frame_info) {
                std::printf("[INFO] %s first frame: %dx%d channels=%d type=%d\n",
                            device.c_str(), frame.cols, frame.rows, frame.channels(), frame.type());
                printed_frame_info = true;
            }

            cv::Mat bgr;
            if (!ConvertCameraFrameToBgr(device, frame, bgr)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            cv::Mat gray;
            cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
            double mean_brightness = cv::mean(gray)[0];
            if (mean_brightness < 2.0) {
                dark_frame_count++;
                if (dark_frame_count % 200 == 1) {
                    std::fprintf(stderr, "[WARN] %s dark frame mean=%.2f count=%d\n",
                                 device.c_str(), mean_brightness, dark_frame_count);
                }
            } else {
                dark_frame_count = 0;
            }

            cv::resize(gray, gray, cv::Size(kInputSize, kInputSize));
            cv::Mat display;
            cv::resize(bgr, display, cv::Size(kInputSize, kInputSize));

            {
                std::lock_guard<std::mutex> lock(store->mutex);
                store->gray640 = gray.clone();
                store->display = display.clone();
                store->seq++;
                store->ready = true;
            }
        } catch (const cv::Exception& e) {
            std::fprintf(stderr, "[ERROR] %s frame convert failed: %s\n", device.c_str(), e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
}

static bool CopyLatestFrame(FrameStore& store, uint64_t& last_seq, cv::Mat& gray) {
    std::lock_guard<std::mutex> lock(store.mutex);
    if (!store.ready || store.seq == last_seq || store.gray640.empty()) {
        return false;
    }
    last_seq = store.seq;
    gray = store.gray640.clone();
    return true;
}

static bool RunOcrOnFrame(ppocr_system_app_context* ctx, const std::string& expected_model,
                          const cv::Mat& gray, OcrState& state) {
    if (!ctx || gray.empty()) {
        return false;
    }

    ppocr_det_postprocess_params params;
    params.threshold = 0.3f;
    params.box_threshold = 0.6f;
    params.use_dilate = false;
    params.db_score_mode = (char*)"slow";
    params.db_box_type = (char*)"poly";
    params.db_unclip_ratio = 2.0f;

    cv::Mat rgb;
    cv::cvtColor(gray, rgb, cv::COLOR_GRAY2RGB);

    image_buffer_t img;
    memset(&img, 0, sizeof(img));
    img.width = rgb.cols;
    img.height = rgb.rows;
    img.width_stride = rgb.cols;
    img.height_stride = rgb.rows;
    img.format = IMAGE_FORMAT_RGB888;
    img.virt_addr = rgb.data;
    img.size = rgb.cols * rgb.rows * 3;

    ppocr_text_recog_array_result_t results;
    memset(&results, 0, sizeof(results));
    if (inference_ppocrv5_model(ctx, &img, &params, &results) != 0) {
        return false;
    }

    OcrState next_state;
    next_state.ready = true;
    std::string raw;
    for (int i = 0; i < results.count; ++i) {
        if (results.text_result[i].text.score > 0.0f &&
            results.text_result[i].text.str[0] != '\0') {
            raw += results.text_result[i].text.str;
        }
    }
    next_state.raw_text = raw;
    next_state.filtered_text = NormalizeText(raw);
    next_state.model_ok = MatchModelName(expected_model, next_state.filtered_text);
    memcpy(&next_state.results, &results, sizeof(results));
    state = next_state;
    return true;
}

static bool RunChipInspectionOnFrame(ppocr_system_app_context* ocr_ctx,
                                     const std::string& expected_model,
                                     YoloDetector* defect_detector,
                                     const cv::Mat& gray,
                                     OcrState& ocr,
                                     DetectionState& defect) {
    ocr = OcrState();
    defect = DetectionState();

    bool ocr_ok = RunOcrOnFrame(ocr_ctx, expected_model, gray, ocr);
    if (!ocr_ok) {
        ocr.ready = true;
        ocr.model_ok = false;
    }

    bool defect_ok = false;
    if (defect_detector) {
        std::vector<Detection> detections;
        defect_ok = defect_detector->Detect(gray, detections);
        if (defect_ok) {
            defect.ready = true;
            defect.detections = detections;
        }
    }

    return ocr_ok && defect_ok;
}

static bool SerialArrivalMatched(std::string& pending,
                                 const std::string& arrival_command,
                                 size_t& match_count) {
    match_count = 0;
    if (arrival_command.empty() || arrival_command == "ANY") {
        size_t line_end = std::string::npos;
        while ((line_end = pending.find_first_of("\r\n")) != std::string::npos) {
            std::string line = TrimAscii(pending.substr(0, line_end));
            pending.erase(0, line_end + 1);
            if (!line.empty()) {
                match_count++;
            }
        }
        return match_count > 0;
    }

    size_t pos = std::string::npos;
    while ((pos = pending.find(arrival_command)) != std::string::npos) {
        pending.erase(0, pos + arrival_command.size());
        match_count++;
    }
    if (pending.size() > 1024) {
        pending.erase(0, pending.size() - 1024);
    }
    return match_count > 0;
}

static void SerialReceiveThread(SerialPort* serial, const std::string& arrival_command) {
    std::string pending;
    uint8_t buffer[128];
    while (g_running.load()) {
        int n = serial->Receive(buffer, sizeof(buffer), 100);
        if (n < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (n == 0) {
            continue;
        }

        std::string chunk((char*)buffer, (size_t)n);
        pending += chunk;
        size_t match_count = 0;
        if (SerialArrivalMatched(pending, arrival_command, match_count)) {
            uint64_t total = g_chip_arrival_requests.fetch_add(match_count) + match_count;
            std::printf("[SERIAL] received arrival command count=%zu total=%llu data=%s\n",
                        match_count,
                        (unsigned long long)total,
                        PrintableSerialText(chunk).c_str());
            std::fflush(stdout);
        } else {
            std::printf("[SERIAL] received data=%s\n", PrintableSerialText(chunk).c_str());
            std::fflush(stdout);
        }
    }
}

static bool SendMotorMoveCommand(SerialPort* serial,
                                 const std::string& move_command_template,
                                 int current_slot,
                                 int next_slot) {
    if (!serial || !serial->IsOpen()) {
        return false;
    }
    std::string command = FormatMotorCommand(move_command_template, current_slot, next_slot);
    int sent = serial->Send((const uint8_t*)command.data(), command.size());
    if (sent != (int)command.size()) {
        std::fprintf(stderr, "[ERROR] serial send failed slot=%d next=%d sent=%d size=%zu\n",
                     current_slot, next_slot, sent, command.size());
        return false;
    }
    std::printf("[SERIAL] sent move command slot=%d next=%d data=%s\n",
                current_slot, next_slot, PrintableSerialText(command).c_str());
    std::fflush(stdout);
    return true;
}

static void OcrThread(ppocr_system_app_context* ctx, const std::string& expected_model) {
    ppocr_det_postprocess_params params;
    params.threshold = 0.3f;
    params.box_threshold = 0.6f;
    params.use_dilate = false;
    params.db_score_mode = (char*)"slow";
    params.db_box_type = (char*)"poly";
    params.db_unclip_ratio = 2.0f;

    uint64_t last_seq = 0;
    uint64_t consumed_frames = 0;
    while (g_running.load()) {
        cv::Mat gray;
        if (!CopyLatestFrame(g_chip_frame, last_seq, gray)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        consumed_frames++;
        if (consumed_frames % kOcrIntervalFrames != 0) {
            continue;
        }

        cv::Mat rgb;
        cv::cvtColor(gray, rgb, cv::COLOR_GRAY2RGB);
        image_buffer_t img;
        memset(&img, 0, sizeof(img));
        img.width = rgb.cols;
        img.height = rgb.rows;
        img.width_stride = rgb.cols;
        img.height_stride = rgb.rows;
        img.format = IMAGE_FORMAT_RGB888;
        img.virt_addr = rgb.data;
        img.size = rgb.cols * rgb.rows * 3;

        ppocr_text_recog_array_result_t results;
        memset(&results, 0, sizeof(results));
        OcrState state;
        state.ready = true;
        if (inference_ppocrv5_model(ctx, &img, &params, &results) == 0) {
            std::string raw;
            for (int i = 0; i < results.count; ++i) {
                if (results.text_result[i].text.score > 0.0f &&
                    results.text_result[i].text.str[0] != '\0') {
                    raw += results.text_result[i].text.str;
                }
            }
            state.raw_text = raw;
            state.filtered_text = NormalizeText(raw);
            state.model_ok = MatchModelName(expected_model, state.filtered_text);
            memcpy(&state.results, &results, sizeof(results));
        }

        {
            std::lock_guard<std::mutex> lock(g_ocr_mutex);
            g_ocr_state = state;
        }
    }
}

static void YoloThread(const std::string& name, FrameStore* frame_store, YoloDetector* detector,
                       std::mutex* state_mutex, DetectionState* state) {
    uint64_t last_seq = 0;
    uint64_t seen_frames = 0;
    uint64_t ok_count = 0;
    uint64_t fail_count = 0;
    size_t last_detection_count = 0;
    auto last_log = std::chrono::steady_clock::now();

    std::printf("[INFO] %s thread started\n", name.c_str());
    std::fflush(stdout);

    while (g_running.load()) {
        cv::Mat gray;
        if (!CopyLatestFrame(*frame_store, last_seq, gray)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        seen_frames++;
        std::vector<Detection> detections;
        if (detector->Detect(gray, detections)) {
            ok_count++;
            last_detection_count = detections.size();
            std::lock_guard<std::mutex> lock(*state_mutex);
            state->ready = true;
            state->detections = detections;
        } else {
            fail_count++;
        }

        auto now = std::chrono::steady_clock::now();
        if (now - last_log >= std::chrono::seconds(2)) {
            std::printf("[INFO] %s infer frames=%llu ok=%llu fail=%llu last_seq=%llu detections=%zu\n",
                        name.c_str(),
                        (unsigned long long)seen_frames,
                        (unsigned long long)ok_count,
                        (unsigned long long)fail_count,
                        (unsigned long long)last_seq,
                        last_detection_count);
            std::fflush(stdout);
            last_log = now;
        }
    }
}

static void DrawOcr(cv::Mat& image, const OcrState& state) {
    cv::putText(image, "video21 OCR", cv::Point(12, image.rows - 16),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
    if (!state.ready) {
        cv::putText(image, "OCR waiting", cv::Point(12, 32),
                    cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(180, 180, 180), 2);
        return;
    }

    cv::Scalar color = state.model_ok ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
    cv::putText(image, state.model_ok ? "OCR OK" : "OCR NG", cv::Point(12, 32),
                cv::FONT_HERSHEY_SIMPLEX, 0.9, color, 2);
    const float scale_x = (float)image.cols / (float)kInputSize;
    const float scale_y = (float)image.rows / (float)kInputSize;
    for (int i = 0; i < state.results.count; ++i) {
        const auto& box = state.results.text_result[i].box;
        std::vector<cv::Point> pts = {
            {(int)std::round(box.left_top.x * scale_x), (int)std::round(box.left_top.y * scale_y)},
            {(int)std::round(box.right_top.x * scale_x), (int)std::round(box.right_top.y * scale_y)},
            {(int)std::round(box.right_bottom.x * scale_x), (int)std::round(box.right_bottom.y * scale_y)},
            {(int)std::round(box.left_bottom.x * scale_x), (int)std::round(box.left_bottom.y * scale_y)}
        };
        cv::polylines(image, pts, true, cv::Scalar(255, 0, 0), 2);
    }
    if (!state.filtered_text.empty()) {
        cv::putText(image, state.filtered_text.substr(0, 32), cv::Point(12, 64),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);
    }
}

static void DrawDetections(cv::Mat& image, const std::vector<Detection>& detections,
                           const YoloDetector& detector, const cv::Scalar& color) {
    const float scale_x = (float)image.cols / (float)kInputSize;
    const float scale_y = (float)image.rows / (float)kInputSize;
    for (const auto& det : detections) {
        cv::Rect box(
            (int)std::round(det.box.x * scale_x),
            (int)std::round(det.box.y * scale_y),
            (int)std::round(det.box.width * scale_x),
            (int)std::round(det.box.height * scale_y));
        box &= cv::Rect(0, 0, image.cols, image.rows);
        if (box.empty()) {
            continue;
        }

        cv::rectangle(image, box, color, 2);
        char text[128];
        std::snprintf(text, sizeof(text), "%s %.1f%%", detector.Label(det.cls_id).c_str(), det.score * 100.0f);
        int y = std::max(20, box.y - 6);
        cv::putText(image, text, cv::Point(box.x, y), cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
    }
}

static void DrawWindowTitle(cv::Mat& image, const std::string& title) {
    cv::putText(image, title, cv::Point(12, image.rows - 16),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
}

static void DrawDefect(cv::Mat& image, const DetectionState& state,
                       const YoloDetector& detector) {
    DrawWindowTitle(image, "video21 defect");
    if (!state.ready) {
        cv::putText(image, "defect waiting", cv::Point(12, 32),
                    cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(180, 180, 180), 2);
        return;
    }

    const cv::Scalar color = state.detections.empty()
        ? cv::Scalar(0, 255, 0)
        : cv::Scalar(0, 0, 255);
    std::string status = state.detections.empty()
        ? "DEFECT OK detections=0"
        : "DEFECT NG detections=" + std::to_string(state.detections.size());
    cv::putText(image, status, cv::Point(12, 32), cv::FONT_HERSHEY_SIMPLEX, 0.8, color, 2);
    DrawDetections(image, state.detections, detector, color);
}

static cv::Mat MakePlaceholder(const std::string& title, const std::string& detail) {
    cv::Mat image = cv::Mat::zeros(kInputSize, kInputSize, CV_8UC3);
    cv::putText(image, title, cv::Point(24, 280), cv::FONT_HERSHEY_SIMPLEX,
                0.9, cv::Scalar(255, 255, 255), 2);
    cv::putText(image, detail, cv::Point(24, 320), cv::FONT_HERSHEY_SIMPLEX,
                0.65, cv::Scalar(180, 180, 180), 2);
    return image;
}

static std::string JoinReasons(const std::vector<std::string>& reasons) {
    if (reasons.empty()) {
        return "";
    }
    std::ostringstream oss;
    for (size_t i = 0; i < reasons.size(); ++i) {
        if (i) {
            oss << "; ";
        }
        oss << reasons[i];
    }
    return oss.str();
}

static const char* JsonBool(bool value) {
    return value ? "true" : "false";
}

static float OcrConfidence(const OcrState& state) {
    if (!state.ready || state.results.count <= 0) {
        return 0.0f;
    }
    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < state.results.count; ++i) {
        const float score = state.results.text_result[i].text.score;
        if (score > 0.0f) {
            sum += score;
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<float>(count) : 0.0f;
}

static std::string DetectionListJson(const DetectionState& state, const YoloDetector& detector) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < state.detections.size(); ++i) {
        const Detection& det = state.detections[i];
        if (i) {
            oss << ",";
        }
        oss << "{"
            << "\"class_id\":" << det.cls_id << ","
            << "\"label\":" << CloudUploader::JsonString(detector.Label(det.cls_id)) << ","
            << "\"score\":" << det.score
            << "}";
    }
    oss << "]";
    return oss.str();
}

static std::string DetectionSummary(const DetectionState& state, const YoloDetector& detector) {
    if (!state.ready) {
        return "等待结果";
    }
    if (state.detections.empty()) {
        return "无";
    }
    std::vector<std::string> labels;
    std::set<std::string> unique_labels;
    for (const Detection& det : state.detections) {
        unique_labels.insert(detector.Label(det.cls_id));
    }
    labels.assign(unique_labels.begin(), unique_labels.end());
    return JoinReasons(labels);
}

static void UploadChipInspection(CloudUploader* uploader,
                                 int slot,
                                 int chip_slots,
                                 bool inference_ok,
                                 const std::string& expected_model,
                                 const OcrState& ocr,
                                 const DetectionState& defect,
                                 const YoloDetector& defect_detector) {
    if (!uploader || !uploader->enabled()) {
        return;
    }

    const bool ocr_pass = ocr.ready && ocr.model_ok;
    std::ostringstream ocr_json;
    ocr_json << "{"
             << "\"slot_index\":" << slot << ","
             << "\"slot_count\":" << chip_slots << ","
             << "\"template\":" << CloudUploader::JsonString(expected_model) << ","
             << "\"raw_text\":" << CloudUploader::JsonString(ocr.raw_text) << ","
             << "\"normalized_text\":" << CloudUploader::JsonString(ocr.filtered_text) << ","
             << "\"matched\":" << JsonBool(ocr_pass) << ","
             << "\"confidence\":" << OcrConfidence(ocr) << ","
             << "\"inference_ok\":" << JsonBool(inference_ok) << ","
             << "\"status\":" << CloudUploader::JsonString(ocr_pass ? "pass" : "ng")
             << "}";
    uploader->PostInspectionResult("ocr", kChipCameraDevice, ocr_json.str());

    const bool defect_pass = defect.ready && defect.detections.empty();
    std::ostringstream defect_json;
    defect_json << "{"
                << "\"slot_index\":" << slot << ","
                << "\"slot_count\":" << chip_slots << ","
                << "\"defect_found\":" << JsonBool(!defect_pass) << ","
                << "\"defect_count\":" << defect.detections.size() << ","
                << "\"defect_summary\":" << CloudUploader::JsonString(DetectionSummary(defect, defect_detector)) << ","
                << "\"detections\":" << DetectionListJson(defect, defect_detector) << ","
                << "\"inference_ok\":" << JsonBool(inference_ok) << ","
                << "\"status\":" << CloudUploader::JsonString(defect_pass ? "pass" : "ng")
                << "}";
    uploader->PostInspectionResult("defect", kChipCameraDevice, defect_json.str());

    if (ocr.ready && !ocr.model_ok) {
        std::ostringstream alarm_json;
        alarm_json << "{"
                   << "\"slot_index\":" << slot << ","
                   << "\"template\":" << CloudUploader::JsonString(expected_model) << ","
                   << "\"recognized_text\":" << CloudUploader::JsonString(ocr.filtered_text) << ","
                   << "\"raw_text\":" << CloudUploader::JsonString(ocr.raw_text)
                   << "}";
        uploader->PostAlarm("ocr", "warning", "text_mismatch", "字符不匹配", alarm_json.str());
    }
    if (defect.ready && !defect.detections.empty()) {
        std::ostringstream alarm_json;
        alarm_json << "{"
                   << "\"slot_index\":" << slot << ","
                   << "\"defect_summary\":" << CloudUploader::JsonString(DetectionSummary(defect, defect_detector)) << ","
                   << "\"detections\":" << DetectionListJson(defect, defect_detector)
                   << "}";
        uploader->PostAlarm("defect", "critical", "surface_defect", "检测到芯片缺陷", alarm_json.str());
    }
}

static void PrintStatusEvery2s(const std::string& expected_model,
                               const YoloDetector& defect_detector,
                               const YoloDetector& fatigue_detector,
                               CloudUploader* uploader) {
    static auto last_print = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    static bool previous_fatigue = false;
    auto now = std::chrono::steady_clock::now();
    if (now - last_print < std::chrono::seconds(2)) {
        return;
    }
    last_print = now;

    OcrState ocr;
    DetectionState defect;
    DetectionState fatigue;
    {
        std::lock_guard<std::mutex> lock(g_ocr_mutex);
        ocr = g_ocr_state;
    }
    {
        std::lock_guard<std::mutex> lock(g_defect_mutex);
        defect = g_defect_state;
    }
    {
        std::lock_guard<std::mutex> lock(g_fatigue_mutex);
        fatigue = g_fatigue_state;
    }

    std::vector<std::string> chip_reasons;
    if (!ocr.ready) {
        chip_reasons.push_back("OCR等待结果");
    } else if (!ocr.model_ok) {
        std::string detected = ocr.filtered_text.empty() ? "未识别" : ocr.filtered_text;
        chip_reasons.push_back("型号不对 expected=" + expected_model + " detected=" + detected);
    }
    if (!defect.ready) {
        chip_reasons.push_back("defect等待结果");
    } else {
        std::set<std::string> defect_names;
        for (const auto& det : defect.detections) {
            defect_names.insert(defect_detector.Label(det.cls_id));
        }
        for (const auto& name : defect_names) {
            chip_reasons.push_back("检测到" + name);
        }
    }

    if (chip_reasons.empty()) {
        std::printf("[CHIP] 良品 expected=%s ocr=%s defect=0\n",
                    expected_model.c_str(), ocr.filtered_text.c_str());
    } else {
        std::printf("[CHIP] 次品/判定中 reason=%s\n", JoinReasons(chip_reasons).c_str());
    }

    bool is_fatigue = false;
    std::string fatigue_reason = "正常";
    if (!fatigue.ready) {
        fatigue_reason = "等待结果";
    } else {
        for (const auto& det : fatigue.detections) {
            std::string label = fatigue_detector.Label(det.cls_id);
            if (label == "close_eye" || label == "open_mouth") {
                is_fatigue = true;
                fatigue_reason = label + " score=" + std::to_string(det.score);
                break;
            }
        }
    }
    std::printf("[FATIGUE] %s reason=%s\n", is_fatigue ? "疲劳" : "未疲劳", fatigue_reason.c_str());
    if (uploader && uploader->enabled() && fatigue.ready) {
        std::ostringstream result_json;
        result_json << "{"
                    << "\"fatigue_detected\":" << JsonBool(is_fatigue) << ","
                    << "\"reason\":" << CloudUploader::JsonString(fatigue_reason) << ","
                    << "\"detection_count\":" << fatigue.detections.size() << ","
                    << "\"detections\":" << DetectionListJson(fatigue, fatigue_detector) << ","
                    << "\"status\":" << CloudUploader::JsonString(is_fatigue ? "warning" : "pass")
                    << "}";
        uploader->PostInspectionResult("fatigue", "/dev/video23", result_json.str());
    }
    if (is_fatigue && !previous_fatigue) {
        AudioAlert::PlayFatigueWarningAsync();
        if (uploader && uploader->enabled()) {
            std::ostringstream alarm_json;
            alarm_json << "{"
                       << "\"fatigue_detected\":true,"
                       << "\"reason\":" << CloudUploader::JsonString(fatigue_reason) << ","
                       << "\"detections\":" << DetectionListJson(fatigue, fatigue_detector)
                       << "}";
            uploader->PostAlarm("fatigue", "warning", "fatigue_detected", "检测到疲劳告警", alarm_json.str());
        }
    }
    previous_fatigue = is_fatigue;
    std::fflush(stdout);
}

int main(int argc, char** argv) {
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    std::string chip_camera = kChipCameraDevice;
    std::string fatigue_camera = "/dev/video23";
    std::string serial_port_name = kDefaultSerialPort;
    int serial_baudrate = 115200;
    std::string arrival_command = kDefaultArrivalCommand;
    std::string move_command = kDefaultMoveCommand;
    std::string expected_model_arg;
    std::string cloud_config_path = kDefaultCloudUploadConfig;
    bool cloud_upload_enabled = true;
    int chip_slots = kDefaultChipSlots;
    bool show_window = true;
    bool display_only = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--chip-camera" && i + 1 < argc) {
            std::cerr << "[WARN] --chip-camera is fixed to " << kChipCameraDevice
                      << ", ignore " << argv[++i] << "\n";
        } else if (arg == "--fatigue-camera" && i + 1 < argc) {
            fatigue_camera = argv[++i];
        } else if (arg == "--serial-port" && i + 1 < argc) {
            serial_port_name = argv[++i];
        } else if (arg == "--serial-baud" && i + 1 < argc) {
            serial_baudrate = std::stoi(argv[++i]);
        } else if (arg == "--arrival-command" && i + 1 < argc) {
            arrival_command = DecodeEscapedCommand(argv[++i]);
        } else if (arg == "--move-command" && i + 1 < argc) {
            move_command = DecodeEscapedCommand(argv[++i]);
        } else if (arg == "--chip-slots" && i + 1 < argc) {
            chip_slots = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--chip-model" && i + 1 < argc) {
            expected_model_arg = argv[++i];
        } else if (arg == "--cloud-config" && i + 1 < argc) {
            cloud_config_path = argv[++i];
        } else if (arg == "--no-cloud-upload") {
            cloud_upload_enabled = false;
        } else if (arg == "--no-window") {
            show_window = false;
        } else if (arg == "--window" || arg == "--show-window") {
            show_window = true;
        } else if (arg == "--display-only") {
            display_only = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ./main_process [--fatigue-camera /dev/video23] [--serial-port /dev/ttyS9]\n";
            std::cout << "                      [--serial-baud 115200] [--arrival-command DONE]\n";
            std::cout << "                      [--move-command 'MOVE_NEXT\\n'] [--chip-slots 4]\n";
            std::cout << "                      [--chip-model STM32F103C8T6]\n";
            std::cout << "                      [--cloud-config config/cloud_upload.ini] [--no-cloud-upload]\n";
            std::cout << "                      [--no-window] [--display-only]\n";
            std::cout << "Chip OCR/defect camera is fixed to " << kChipCameraDevice << "\n";
            std::cout << "Move command supports {current} and {next} placeholders.\n";
            return 0;
        }
    }

    std::string expected_model;
    if (display_only) {
        expected_model = "DISPLAY";
    } else {
        expected_model = NormalizeText(expected_model_arg);
        if (expected_model.empty()) {
            expected_model = "DEFAULT";
            std::cerr << "[WARN] --chip-model not set, use DEFAULT for OCR match\n";
        }
    }

    std::cout << "[INFO] input image: 640x640 gray\n";
    std::cout << "[INFO] chip camera: " << chip_camera << " (OCR + defect)\n";
    std::cout << "[INFO] fatigue camera: " << fatigue_camera << "\n";
    std::cout << "[INFO] NPU core: OCR=core0, fatigue=core1, defect=core2\n";
    std::cout << "[INFO] video21 schedule: wait serial arrival, inspect one chip, then move next\n";
    std::cout << "[INFO] chip slots: " << chip_slots << "\n";
    std::cout << "[INFO] serial: port=" << serial_port_name
              << " baud=" << serial_baudrate
              << " arrival=" << PrintableSerialText(arrival_command)
              << " move=" << PrintableSerialText(move_command) << "\n";
    if (display_only) {
        std::cout << "[INFO] display-only mode: models disabled\n";
        return RunDisplayOnlyFromCaptureService(chip_camera);
    }

    CloudUploader cloud_uploader;
    CloudUploader* uploader = nullptr;
    if (cloud_upload_enabled && cloud_uploader.Start(cloud_config_path)) {
        uploader = &cloud_uploader;
        cloud_uploader.UpdateCameraStatus(false, false);
    }

    setenv("RKNN_LOG_LEVEL", "0", 0);
    setenv("RGA_LOG_LEVEL", "3", 0);

    ppocr_system_app_context ocr_ctx;
    memset(&ocr_ctx, 0, sizeof(ocr_ctx));
    if (!display_only) {
        if (init_ppocr_model("model/ocr/PP-OCRv5_mobile_det.rknn", &ocr_ctx.det_context) != 0 ||
            init_ppocr_rec_model("model/ocr/PP-OCRv5_mobile_rec.rknn", &ocr_ctx.rec_context) != 0) {
            std::cerr << "[ERROR] OCR init failed\n";
            return 1;
        }
        rknn_set_core_mask(ocr_ctx.det_context.rknn_ctx, (rknn_core_mask)RKNN_NPU_CORE_0);
        rknn_set_core_mask(ocr_ctx.rec_context.rknn_ctx, (rknn_core_mask)RKNN_NPU_CORE_0);
    }

    YoloDetector defect_detector;
    if (!display_only) {
        if (!defect_detector.Init("model/defect/defect_best_i8.rknn", "model/defect/dataset.txt",
                                  2, kDefectThreshold, (rknn_core_mask)RKNN_NPU_CORE_2)) {
            std::cerr << "[ERROR] defect init failed\n";
            return 1;
        }
    }

    YoloDetector fatigue_detector;
    if (!display_only) {
        if (!fatigue_detector.Init("model/fatigue/fatigue_two_outputs_i8.rknn", "model/fatigue/dataset.txt",
                                   3, kFatigueThreshold, (rknn_core_mask)RKNN_NPU_CORE_1)) {
            std::cerr << "[ERROR] fatigue init failed\n";
            return 1;
        }
    }

    SerialPort serial;
    std::thread serial_worker;
    if (!display_only) {
        SerialConfig serial_config;
        serial_config.port = serial_port_name;
        serial_config.baudrate = serial_baudrate;
        serial_config.databits = 8;
        serial_config.stopbits = 1;
        serial_config.parity = 'N';
        if (!serial.Open(serial_config)) {
            std::cerr << "[ERROR] failed to open serial port " << serial_port_name << "\n";
            release_ppocr_model(&ocr_ctx.det_context);
            release_ppocr_model(&ocr_ctx.rec_context);
            defect_detector.Release();
            fatigue_detector.Release();
            return 1;
        }
        serial_worker = std::thread(SerialReceiveThread, &serial, arrival_command);
        std::cout << "[INFO] waiting for motor arrival command before chip 1 inspection\n";
    }

    pid_t chip_capture_pid = StartCameraCaptureService(chip_camera, kChipFramePath);
    if (chip_capture_pid <= 0) {
        std::cerr << "[ERROR] failed to start camera_capture_service for " << chip_camera << "\n";
        g_running.store(false);
        if (serial_worker.joinable()) serial_worker.join();
        serial.Close();
        release_ppocr_model(&ocr_ctx.det_context);
        release_ppocr_model(&ocr_ctx.rec_context);
        defect_detector.Release();
        fatigue_detector.Release();
        return 1;
    }
    std::cout << "[INFO] chip camera capture service started: pid=" << chip_capture_pid
              << " frame=" << kChipFramePath << "\n";
    if (uploader) {
        uploader->UpdateCameraStatus(true, false);
    }

    std::thread chip_ocr_worker;
    std::thread chip_defect_worker;
    std::thread fatigue_capture;
    if (!display_only) {
        fatigue_capture = std::thread(CaptureThread, fatigue_camera, &g_fatigue_frame);
        if (uploader) {
            uploader->UpdateCameraStatus(true, true);
        }
    }
    std::thread fatigue_worker;
    if (!display_only) {
        fatigue_worker = std::thread(YoloThread, "fatigue", &g_fatigue_frame, &fatigue_detector,
                                     &g_fatigue_mutex, &g_fatigue_state);
    }

    if (show_window) {
        try {
            ConfigureDisplayEnv();
            cv::namedWindow("video21 OCR", cv::WINDOW_NORMAL);
            cv::namedWindow("video21 defect", cv::WINDOW_NORMAL);
            cv::namedWindow("video23 fatigue", cv::WINDOW_NORMAL);
            std::cout << "[INFO] OpenCV windows enabled: video21 OCR, video21 defect, video23 fatigue\n";
        } catch (const cv::Exception& e) {
            std::cerr << "[WARN] disable windows: " << e.what() << "\n";
            show_window = false;
        }
    }

    bool printed_chip_frame_info = false;
    int empty_chip_frame_count = 0;
    uint64_t handled_arrival_requests = 0;
    int current_chip_slot = 1;

    while (g_running.load()) {
        cv::Mat chip_frame;
        if (!ReadLatestChipFrame(chip_frame)) {
            empty_chip_frame_count++;
            if (empty_chip_frame_count % 200 == 0) {
                std::cerr << "[WARN] waiting for chip frame file count="
                          << empty_chip_frame_count << " path=" << kChipFramePath << "\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        empty_chip_frame_count = 0;

        if (!printed_chip_frame_info) {
            std::cout << "[INFO] " << chip_camera << " first frame: "
                      << chip_frame.cols << "x" << chip_frame.rows
                      << " channels=" << chip_frame.channels()
                      << " type=" << chip_frame.type() << "\n";
            printed_chip_frame_info = true;
        }

        cv::Mat chip_display = chip_frame.clone();
        cv::Mat chip_gray;
        cv::Mat unused_display;
        if (!display_only && !PrepareChipFrame(chip_camera, chip_frame, chip_gray, unused_display)) {
            continue;
        }

        if (!display_only) {
            std::lock_guard<std::mutex> lock(g_chip_frame.mutex);
            g_chip_frame.gray640 = chip_gray.clone();
            g_chip_frame.display = unused_display.clone();
            g_chip_frame.seq++;
            g_chip_frame.ready = true;
        }

        if (!display_only) {
            uint64_t requested = g_chip_arrival_requests.load();
            if (requested > handled_arrival_requests) {
                handled_arrival_requests++;
                int slot = current_chip_slot;
                int next_slot = slot >= chip_slots ? 1 : slot + 1;
                std::printf("[CHIP] start inspection slot=%d/%d request=%llu\n",
                            slot, chip_slots, (unsigned long long)handled_arrival_requests);
                std::fflush(stdout);

                OcrState ocr_result;
                DetectionState defect_result;
                bool inference_ok = RunChipInspectionOnFrame(&ocr_ctx,
                                                             expected_model,
                                                             &defect_detector,
                                                             chip_gray,
                                                             ocr_result,
                                                             defect_result);
                {
                    std::lock_guard<std::mutex> lock(g_ocr_mutex);
                    g_ocr_state = ocr_result;
                }
                {
                    std::lock_guard<std::mutex> lock(g_defect_mutex);
                    g_defect_state = defect_result;
                }

                const bool chip_ok = ocr_result.ready && ocr_result.model_ok &&
                                     defect_result.ready && defect_result.detections.empty();
                std::printf("[CHIP] finish slot=%d/%d result=%s inference=%s ocr=%s defect_count=%zu\n",
                            slot,
                            chip_slots,
                            chip_ok ? "OK" : "NG",
                            inference_ok ? "OK" : "FAIL",
                            ocr_result.filtered_text.empty() ? "EMPTY" : ocr_result.filtered_text.c_str(),
                            defect_result.detections.size());
                std::fflush(stdout);

                UploadChipInspection(uploader,
                                     slot,
                                     chip_slots,
                                     inference_ok,
                                     expected_model,
                                     ocr_result,
                                     defect_result,
                                     defect_detector);

                SendMotorMoveCommand(&serial, move_command, slot, next_slot);
                current_chip_slot = next_slot;
            }
        }

        cv::Mat fatigue_display;
        {
            std::lock_guard<std::mutex> lock(g_fatigue_frame.mutex);
            if (!g_fatigue_frame.display.empty()) {
                fatigue_display = g_fatigue_frame.display.clone();
            }
        }
        if (fatigue_display.empty()) {
            fatigue_display = MakePlaceholder("video23 loading", fatigue_camera);
        }

        OcrState ocr;
        DetectionState defect;
        DetectionState fatigue;
        {
            std::lock_guard<std::mutex> lock(g_ocr_mutex);
            ocr = g_ocr_state;
        }
        {
            std::lock_guard<std::mutex> lock(g_defect_mutex);
            defect = g_defect_state;
        }
        {
            std::lock_guard<std::mutex> lock(g_fatigue_mutex);
            fatigue = g_fatigue_state;
        }

        cv::Mat chip_ocr_display = chip_display.clone();
        cv::Mat chip_defect_display = chip_display.clone();
        if (display_only) {
            DrawWindowTitle(chip_ocr_display, "video21 OCR display-only");
            DrawWindowTitle(chip_defect_display, "video21 defect display-only");
        } else {
            DrawOcr(chip_ocr_display, ocr);
            DrawDefect(chip_defect_display, defect, defect_detector);
        }
        if (show_window) {
            try {
                cv::imshow("video21 OCR", chip_ocr_display);
                cv::imshow("video21 defect", chip_defect_display);
            } catch (const cv::Exception& e) {
                std::cerr << "[WARN] disable windows: " << e.what() << "\n";
                show_window = false;
            }
        }
        if (!display_only) {
            DrawDetections(fatigue_display, fatigue.detections, fatigue_detector, cv::Scalar(0, 255, 255));
        }
        DrawWindowTitle(fatigue_display, "video23 fatigue");
        if (show_window) {
            try {
                cv::imshow("video23 fatigue", fatigue_display);
            } catch (const cv::Exception& e) {
                std::cerr << "[WARN] disable windows: " << e.what() << "\n";
                show_window = false;
            }
        }
        if (show_window) {
            int key = cv::waitKey(1);
            if (key == 27 || key == 'q' || key == 'Q') {
                g_running.store(false);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        if (!display_only) {
            PrintStatusEvery2s(expected_model, defect_detector, fatigue_detector, uploader);
        }
    }

    if (chip_ocr_worker.joinable()) chip_ocr_worker.join();
    if (chip_defect_worker.joinable()) chip_defect_worker.join();
    if (fatigue_capture.joinable()) fatigue_capture.join();
    if (fatigue_worker.joinable()) fatigue_worker.join();
    if (serial_worker.joinable()) serial_worker.join();

    StopCameraCaptureService(chip_capture_pid);
    if (uploader) {
        uploader->UpdateCameraStatus(false, false);
        uploader->Stop();
    }
    serial.Close();
    if (!display_only) {
        release_ppocr_model(&ocr_ctx.det_context);
        release_ppocr_model(&ocr_ctx.rec_context);
        defect_detector.Release();
        fatigue_detector.Release();
    }
    if (show_window) {
        cv::destroyAllWindows();
    }
    return 0;
}
