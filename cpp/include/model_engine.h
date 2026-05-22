#ifndef MODEL_ENGINE_H
#define MODEL_ENGINE_H

#include "common.h"
#include <rknn_api.h>
#include <string>

// ============================================================
// 模型引擎基类
// ============================================================
// 所有模型引擎（OCR、疲劳检测、缺陷检测）都继承此基类
// 提供统一的模型加载、推理、释放接口
// ============================================================

class ModelEngine {
public:
    ModelEngine();
    virtual ~ModelEngine();

    // 初始化模型引擎
    // model_path: 模型文件路径
    // core_mask: NPU 核心掩码
    virtual bool Init(const std::string& model_path, NpuCoreMask core_mask);

    // 释放模型资源
    virtual void Release();

    // 模型推理
    // input_data: 输入图像数据
    // width: 图像宽度
    // height: 图像高度
    // channels: 图像通道数
    virtual bool Inference(const uint8_t* input_data, int width, int height, int channels) = 0;

    // 获取模型输入尺寸
    virtual int GetInputWidth() const = 0;
    virtual int GetInputHeight() const = 0;

    // 获取模型名称
    virtual std::string GetModelName() const = 0;

    // 检查模型是否已初始化
    bool IsInitialized() const { return initialized_; }

protected:
    // RKNN 上下文
    rknn_context rknn_ctx_;

    // 模型输入输出信息
    rknn_tensor_attr* input_attrs_;
    rknn_tensor_attr* output_attrs_;
    uint32_t n_input_;
    uint32_t n_output_;

    // NPU 核心掩码
    NpuCoreMask core_mask_;

    // 初始化标志
    bool initialized_;

    // 模型路径
    std::string model_path_;
};

#endif // MODEL_ENGINE_H
