#include "fatigue_engine.h"
#include "logger.h"
#include <rknn_api.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>

// ============================================================
// 构造函数和析构函数
// ============================================================
FatigueEngine::FatigueEngine()
    : input_width_(0),
      input_height_(0),
      fatigue_threshold_(0.5f),
      fatigue_frame_count_(0),
      fatigue_frame_threshold_(3) {
}

FatigueEngine::~FatigueEngine() {
    Release();
}

// ============================================================
// 初始化疲劳检测引擎
// ============================================================
bool FatigueEngine::Init(const std::string& model_path, const std::string& labels_path) {
    // 加载标签
    if (!LoadLabels(labels_path)) {
        LOG_ERROR("Failed to load labels");
        return false;
    }

    // 初始化模型
    if (!ModelEngine::Init(model_path, NPU_CORE_1)) {
        LOG_ERROR("Failed to init fatigue model");
        return false;
    }

    input_width_ = GetInputWidth();
    input_height_ = GetInputHeight();

    LOG_INFO("Fatigue engine initialized");
    LOG_INFO("  Model: %s (%dx%d)", model_path.c_str(), input_width_, input_height_);
    LOG_INFO("  Labels: %zu", labels_.size());

    return true;
}

// ============================================================
// 释放资源
// ============================================================
void FatigueEngine::Release() {
    ModelEngine::Release();
}

// ============================================================
// 疲劳检测
// ============================================================
bool FatigueEngine::Detect(const uint8_t* image_data, int width, int height, int channels,
                           FatigueResult& result) {
    if (!initialized_) {
        LOG_ERROR("Fatigue engine not initialized");
        return false;
    }

    // 执行推理
    if (!Inference(image_data, width, height, channels)) {
        LOG_ERROR("Inference failed");
        return false;
    }

    // 获取输出
    rknn_output outputs[n_output_];
    memset(outputs, 0, sizeof(outputs));

    for (uint32_t i = 0; i < n_output_; i++) {
        outputs[i].want_float = 1;
    }

    int ret = rknn_outputs_get(rknn_ctx_, n_output_, outputs, nullptr);
    if (ret != RKNN_SUCC) {
        LOG_ERROR("Failed to get outputs: %d", ret);
        return false;
    }

    // 后处理
    if (outputs[0].buf) {
        PostProcess(static_cast<float*>(outputs[0].buf),
                   outputs[0].size / sizeof(float),
                   result);
    }

    // 释放输出
    rknn_outputs_release(rknn_ctx_, n_output_, outputs);

    return true;
}

// ============================================================
// 模型推理
// ============================================================
bool FatigueEngine::Inference(const uint8_t* input_data, int width, int height, int channels) {
    // 准备输入
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));

    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].size = width * height * channels;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].buf = const_cast<uint8_t*>(input_data);

    int ret = rknn_inputs_set(rknn_ctx_, 1, inputs);
    if (ret != RKNN_SUCC) {
        LOG_ERROR("Failed to set input: %d", ret);
        return false;
    }

    // 执行推理
    ret = rknn_run(rknn_ctx_, nullptr);
    if (ret != RKNN_SUCC) {
        LOG_ERROR("Failed to run inference: %d", ret);
        return false;
    }

    return true;
}

// ============================================================
// 获取输入尺寸
// ============================================================
int FatigueEngine::GetInputWidth() const {
    if (!input_attrs_ || n_input_ == 0) return 0;

    return input_attrs_[0].dims[2];  // NHWC format
}

int FatigueEngine::GetInputHeight() const {
    if (!input_attrs_ || n_input_ == 0) return 0;

    return input_attrs_[0].dims[1];  // NHWC format
}

// ============================================================
// 加载标签
// ============================================================
bool FatigueEngine::LoadLabels(const std::string& labels_path) {
    std::ifstream file(labels_path);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open labels file: %s", labels_path.c_str());
        return false;
    }

    labels_.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            labels_.push_back(line);
        }
    }

    return true;
}

// ============================================================
// 后处理
// ============================================================
void FatigueEngine::PostProcess(const float* output_data, int output_size,
                                FatigueResult& result) {
    // 找到最大概率的类别
    int max_idx = 0;
    float max_prob = output_data[0];

    for (int i = 1; i < output_size && i < static_cast<int>(labels_.size()); i++) {
        if (output_data[i] > max_prob) {
            max_prob = output_data[i];
            max_idx = i;
        }
    }

    // 填充结果
    if (max_idx < static_cast<int>(labels_.size())) {
        result.category = labels_[max_idx];
    } else {
        result.category = "unknown";
    }

    result.confidence = max_prob;
    result.timestamp = GetCurrentTimestamp();

    // 判断是否疲劳
    result.is_fatigue = IsFatigue(result.category, result.confidence);

    if (result.is_fatigue) {
        fatigue_frame_count_++;
    } else {
        fatigue_frame_count_ = 0;
    }
}

// ============================================================
// 判断是否疲劳
// ============================================================
bool FatigueEngine::IsFatigue(const std::string& category, float confidence) {
    // 闭眼判断为疲劳
    if (category == "close_eye" && confidence > fatigue_threshold_) {
        return true;
    }

    // 连续多帧检测到疲劳状态
    if (fatigue_frame_count_ >= fatigue_frame_threshold_) {
        return true;
    }

    return false;
}
