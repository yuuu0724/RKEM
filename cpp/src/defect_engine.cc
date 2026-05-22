#include "defect_engine.h"
#include "logger.h"
#include <rknn_api.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>

// ============================================================
// 构造函数和析构函数
// ============================================================
DefectEngine::DefectEngine()
    : input_width_(0),
      input_height_(0),
      confidence_threshold_(0.5f),
      nms_threshold_(0.45f) {
}

DefectEngine::~DefectEngine() {
    Release();
}

// ============================================================
// 初始化缺陷检测引擎
// ============================================================
bool DefectEngine::Init(const std::string& model_path, const std::string& labels_path) {
    // 加载标签
    if (!LoadLabels(labels_path)) {
        LOG_ERROR("Failed to load labels");
        return false;
    }

    // 初始化模型
    if (!ModelEngine::Init(model_path, NPU_CORE_2)) {
        LOG_ERROR("Failed to init defect model");
        return false;
    }

    input_width_ = GetInputWidth();
    input_height_ = GetInputHeight();

    LOG_INFO("Defect engine initialized");
    LOG_INFO("  Model: %s (%dx%d)", model_path.c_str(), input_width_, input_height_);
    LOG_INFO("  Labels: %zu", labels_.size());

    return true;
}

// ============================================================
// 释放资源
// ============================================================
void DefectEngine::Release() {
    ModelEngine::Release();
}

// ============================================================
// 缺陷检测
// ============================================================
bool DefectEngine::Detect(const uint8_t* image_data, int width, int height, int channels,
                          std::vector<DefectResult>& results) {
    if (!initialized_) {
        LOG_ERROR("Defect engine not initialized");
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
    for (uint32_t i = 0; i < n_output_; i++) {
        if (outputs[i].buf) {
            PostProcess(static_cast<float*>(outputs[i].buf),
                       outputs[i].size / sizeof(float),
                       results);
        }
    }

    // 释放输出
    rknn_outputs_release(rknn_ctx_, n_output_, outputs);

    return true;
}

// ============================================================
// 模型推理
// ============================================================
bool DefectEngine::Inference(const uint8_t* input_data, int width, int height, int channels) {
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
int DefectEngine::GetInputWidth() const {
    if (!input_attrs_ || n_input_ == 0) return 0;

    return input_attrs_[0].dims[2];  // NHWC format
}

int DefectEngine::GetInputHeight() const {
    if (!input_attrs_ || n_input_ == 0) return 0;

    return input_attrs_[0].dims[1];  // NHWC format
}

// ============================================================
// 加载标签
// ============================================================
bool DefectEngine::LoadLabels(const std::string& labels_path) {
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
void DefectEngine::PostProcess(const float* output_data, int output_size,
                               std::vector<DefectResult>& results) {
    // TODO: 实现缺陷检测后处理
    // 这里需要根据模型输出格式解析检测结果
    // 通常包括边界框、置信度、类别等信息

    // 示例实现（需要根据实际模型输出格式调整）
    for (int i = 0; i < output_size; i += 6) {
        if (i + 5 >= output_size) break;

        float x = output_data[i];
        float y = output_data[i + 1];
        float w = output_data[i + 2];
        float h = output_data[i + 3];
        float confidence = output_data[i + 4];
        int class_id = static_cast<int>(output_data[i + 5]);

        if (confidence < confidence_threshold_) continue;

        DefectResult result;
        result.bbox[0] = static_cast<int>(x);
        result.bbox[1] = static_cast<int>(y);
        result.bbox[2] = static_cast<int>(w);
        result.bbox[3] = static_cast<int>(h);
        result.confidence = confidence;
        result.timestamp = GetCurrentTimestamp();

        if (class_id < static_cast<int>(labels_.size())) {
            result.defect_type = labels_[class_id];
        } else {
            result.defect_type = "unknown";
        }

        results.push_back(result);
    }
}
