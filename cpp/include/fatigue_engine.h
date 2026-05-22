#ifndef FATIGUE_ENGINE_H
#define FATIGUE_ENGINE_H

#include "model_engine.h"
#include "common.h"
#include <vector>

// ============================================================
// 疲劳检测引擎
// ============================================================
// 负责检测眼睛和嘴巴状态
// 判断是否疲劳
// 运行在 NPU Core 1
// ============================================================

class FatigueEngine : public ModelEngine {
public:
    FatigueEngine();
    virtual ~FatigueEngine();

    // 初始化疲劳检测引擎
    // model_path: 模型文件路径
    // labels_path: 标签文件路径
    bool Init(const std::string& model_path, const std::string& labels_path);

    // 释放资源
    void Release() override;

    // 疲劳检测
    // image_data: 输入图像数据
    // width: 图像宽度
    // height: 图像高度
    // channels: 图像通道数
    // result: 输出检测结果
    bool Detect(const uint8_t* image_data, int width, int height, int channels,
                FatigueResult& result);

    // 模型推理（基类接口）
    bool Inference(const uint8_t* input_data, int width, int height, int channels) override;

    // 获取输入尺寸
    int GetInputWidth() const override;
    int GetInputHeight() const override;

    // 获取模型名称
    std::string GetModelName() const override { return "Fatigue"; }

private:
    // 加载标签
    bool LoadLabels(const std::string& labels_path);

    // 后处理
    void PostProcess(const float* output_data, int output_size, FatigueResult& result);

    // 判断是否疲劳
    bool IsFatigue(const std::string& category, float confidence);

private:
    // 标签列表
    std::vector<std::string> labels_;

    // 模型输入尺寸
    int input_width_;
    int input_height_;

    // 疲劳判断阈值
    float fatigue_threshold_;

    // 连续疲劳帧计数
    int fatigue_frame_count_;

    // 连续疲劳帧阈值
    int fatigue_frame_threshold_;
};

#endif // FATIGUE_ENGINE_H
