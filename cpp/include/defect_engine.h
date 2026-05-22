#ifndef DEFECT_ENGINE_H
#define DEFECT_ENGINE_H

#include "model_engine.h"
#include "common.h"
#include <vector>

// ============================================================
// 缺陷检测引擎（预留）
// ============================================================
// 负责检测芯片表面缺陷
// 运行在 NPU Core 2
// ============================================================

class DefectEngine : public ModelEngine {
public:
    DefectEngine();
    virtual ~DefectEngine();

    // 初始化缺陷检测引擎
    // model_path: 模型文件路径
    // labels_path: 标签文件路径
    bool Init(const std::string& model_path, const std::string& labels_path);

    // 释放资源
    void Release() override;

    // 缺陷检测
    // image_data: 输入图像数据
    // width: 图像宽度
    // height: 图像高度
    // channels: 图像通道数
    // results: 输出检测结果
    bool Detect(const uint8_t* image_data, int width, int height, int channels,
                std::vector<DefectResult>& results);

    // 模型推理（基类接口）
    bool Inference(const uint8_t* input_data, int width, int height, int channels) override;

    // 获取输入尺寸
    int GetInputWidth() const override;
    int GetInputHeight() const override;

    // 获取模型名称
    std::string GetModelName() const override { return "Defect"; }

private:
    // 加载标签
    bool LoadLabels(const std::string& labels_path);

    // 后处理
    void PostProcess(const float* output_data, int output_size,
                     std::vector<DefectResult>& results);

private:
    // 标签列表
    std::vector<std::string> labels_;

    // 模型输入尺寸
    int input_width_;
    int input_height_;

    // 置信度阈值
    float confidence_threshold_;

    // NMS 阈值
    float nms_threshold_;
};

#endif // DEFECT_ENGINE_H
