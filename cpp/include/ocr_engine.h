#ifndef OCR_ENGINE_H
#define OCR_ENGINE_H

#include "model_engine.h"
#include "common.h"
#include <vector>
#include <string>

// ============================================================
// OCR 引擎
// ============================================================
// 负责文字检测和识别
// 包含检测模型和识别模型
// 运行在 NPU Core 0
// ============================================================

class OcrEngine : public ModelEngine {
public:
    OcrEngine();
    virtual ~OcrEngine();

    // 初始化 OCR 引擎
    // det_model_path: 检测模型路径
    // rec_model_path: 识别模型路径
    // dict_path: 字典文件路径
    bool Init(const std::string& det_model_path,
              const std::string& rec_model_path,
              const std::string& dict_path);

    // 释放资源
    void Release() override;

    // OCR 识别
    // image_data: 输入图像数据
    // width: 图像宽度
    // height: 图像高度
    // channels: 图像通道数
    // results: 输出识别结果
    bool Recognize(const uint8_t* image_data, int width, int height, int channels,
                   std::vector<OcrResult>& results);

    // 模型推理（基类接口）
    bool Inference(const uint8_t* input_data, int width, int height, int channels) override;

    // 获取输入尺寸
    int GetInputWidth() const override;
    int GetInputHeight() const override;

    // 获取模型名称
    std::string GetModelName() const override { return "OCR"; }

private:
    // 文字检测
    bool Detect(const uint8_t* image_data, int width, int height, int channels,
                std::vector<int>& boxes);

    // 文字识别
    bool RecognizeText(const uint8_t* image_data, int width, int height,
                       const std::vector<int>& boxes, std::vector<OcrResult>& results);

    // 加载字典
    bool LoadDictionary(const std::string& dict_path);

    // 后处理
    void PostProcess(const float* output_data, int output_size,
                     std::vector<OcrResult>& results);

private:
    // 检测模型上下文
    rknn_context det_ctx_;

    // 识别模型上下文
    rknn_context rec_ctx_;

    // 字典
    std::vector<std::string> dictionary_;

    // 检测模型输入尺寸
    int det_input_width_;
    int det_input_height_;

    // 识别模型输入尺寸
    int rec_input_width_;
    int rec_input_height_;
};

#endif // OCR_ENGINE_H
