#include "ocr_engine.h"
#include "logger.h"
#include <rknn_api.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>

// ============================================================
// 构造函数和析构函数
// ============================================================
OcrEngine::OcrEngine()
    : det_ctx_(0),
      rec_ctx_(0),
      det_input_width_(0),
      det_input_height_(0),
      rec_input_width_(0),
      rec_input_height_(0) {
}

OcrEngine::~OcrEngine() {
    Release();
}

// ============================================================
// 初始化 OCR 引擎
// ============================================================
bool OcrEngine::Init(const std::string& det_model_path,
                     const std::string& rec_model_path,
                     const std::string& dict_path) {
    // 加载字典
    if (!LoadDictionary(dict_path)) {
        LOG_ERROR("Failed to load dictionary");
        return false;
    }

    // 初始化检测模型
    if (!ModelEngine::Init(det_model_path, NPU_CORE_0)) {
        LOG_ERROR("Failed to init detection model");
        return false;
    }

    det_ctx_ = rknn_ctx_;
    det_input_width_ = GetInputWidth();
    det_input_height_ = GetInputHeight();

    // 初始化识别模型
    rknn_ctx_ = 0;
    initialized_ = false;

    if (!ModelEngine::Init(rec_model_path, NPU_CORE_0)) {
        LOG_ERROR("Failed to init recognition model");
        return false;
    }

    rec_ctx_ = rknn_ctx_;
    rec_input_width_ = GetInputWidth();
    rec_input_height_ = GetInputHeight();

    // 恢复检测模型上下文
    rknn_ctx_ = det_ctx_;

    LOG_INFO("OCR engine initialized");
    LOG_INFO("  Detection model: %s (%dx%d)", det_model_path.c_str(),
             det_input_width_, det_input_height_);
    LOG_INFO("  Recognition model: %s (%dx%d)", rec_model_path.c_str(),
             rec_input_width_, rec_input_height_);

    return true;
}

// ============================================================
// 释放资源
// ============================================================
void OcrEngine::Release() {
    if (rec_ctx_) {
        rknn_destroy(rec_ctx_);
        rec_ctx_ = 0;
    }

    // 基类释放会处理 det_ctx_
    ModelEngine::Release();
}

// ============================================================
// OCR 识别
// ============================================================
bool OcrEngine::Recognize(const uint8_t* image_data, int width, int height, int channels,
                          std::vector<OcrResult>& results) {
    if (!initialized_) {
        LOG_ERROR("OCR engine not initialized");
        return false;
    }

    // 文字检测
    std::vector<int> boxes;
    if (!Detect(image_data, width, height, channels, boxes)) {
        LOG_ERROR("Text detection failed");
        return false;
    }

    // 文字识别
    if (!RecognizeText(image_data, width, height, boxes, results)) {
        LOG_ERROR("Text recognition failed");
        return false;
    }

    return true;
}

// ============================================================
// 模型推理
// ============================================================
bool OcrEngine::Inference(const uint8_t* input_data, int width, int height, int channels) {
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
int OcrEngine::GetInputWidth() const {
    if (!input_attrs_ || n_input_ == 0) return 0;

    return input_attrs_[0].dims[2];  // NHWC format
}

int OcrEngine::GetInputHeight() const {
    if (!input_attrs_ || n_input_ == 0) return 0;

    return input_attrs_[0].dims[1];  // NHWC format
}

// ============================================================
// 文字检测
// ============================================================
bool OcrEngine::Detect(const uint8_t* image_data, int width, int height, int channels,
                       std::vector<int>& boxes) {
    // 切换到检测模型
    rknn_context saved_ctx = rknn_ctx_;
    rknn_ctx_ = det_ctx_;

    // 执行推理
    if (!Inference(image_data, width, height, channels)) {
        rknn_ctx_ = saved_ctx;
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
        rknn_ctx_ = saved_ctx;
        return false;
    }

    // 后处理检测结果
    // TODO: 实现文字检测后处理
    // 这里需要根据模型输出格式解析文字区域

    // 释放输出
    rknn_outputs_release(rknn_ctx_, n_output_, outputs);
    rknn_ctx_ = saved_ctx;

    return true;
}

// ============================================================
// 文字识别
// ============================================================
bool OcrEngine::RecognizeText(const uint8_t* image_data, int width, int height,
                              const std::vector<int>& boxes, std::vector<OcrResult>& results) {
    // 切换到识别模型
    rknn_context saved_ctx = rknn_ctx_;
    rknn_ctx_ = rec_ctx_;

    // 对每个检测到的文字区域进行识别
    for (size_t i = 0; i < boxes.size(); i += 4) {
        int x1 = boxes[i];
        int y1 = boxes[i + 1];
        int x2 = boxes[i + 2];
        int y2 = boxes[i + 3];

        // 裁剪图像区域
        int crop_w = x2 - x1;
        int crop_h = y2 - y1;

        if (crop_w <= 0 || crop_h <= 0) continue;

        // 执行推理
        // TODO: 实现文字识别推理和后处理
    }

    rknn_ctx_ = saved_ctx;
    return true;
}

// ============================================================
// 加载字典
// ============================================================
bool OcrEngine::LoadDictionary(const std::string& dict_path) {
    std::ifstream file(dict_path);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open dictionary file: %s", dict_path.c_str());
        return false;
    }

    dictionary_.clear();
    std::string line;
    while (std::getline(file, line)) {
        dictionary_.push_back(line);
    }

    LOG_INFO("Loaded %zu dictionary entries", dictionary_.size());
    return true;
}

// ============================================================
// 后处理
// ============================================================
void OcrEngine::PostProcess(const float* output_data, int output_size,
                            std::vector<OcrResult>& results) {
    // TODO: 实现 OCR 后处理
}
