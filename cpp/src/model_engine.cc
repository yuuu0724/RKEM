#include "model_engine.h"
#include "logger.h"
#include <cstring>
#include <cstdio>
#include <cerrno>

// ============================================================
// 构造函数和析构函数
// ============================================================
ModelEngine::ModelEngine()
    : rknn_ctx_(0),
      input_attrs_(nullptr),
      output_attrs_(nullptr),
      n_input_(0),
      n_output_(0),
      core_mask_(NPU_CORE_AUTO),
      initialized_(false) {
}

ModelEngine::~ModelEngine() {
    Release();
}

// ============================================================
// 初始化模型引擎
// ============================================================
bool ModelEngine::Init(const std::string& model_path, NpuCoreMask core_mask) {
    if (initialized_) {
        return true;
    }

    model_path_ = model_path;
    core_mask_ = core_mask;

    // 读取模型文件
    fprintf(stdout, "[DEBUG] Loading model: %s\n", model_path.c_str());
    fflush(stdout);
    FILE* fp = fopen(model_path.c_str(), "rb");
    if (!fp) {
        fprintf(stderr, "[ERROR] Failed to open model file: %s (%s)\n", model_path.c_str(), strerror(errno));
        LOG_ERROR("Failed to open model file: %s", model_path.c_str());
        return false;
    }

    fseek(fp, 0, SEEK_END);
    size_t model_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    void* model_data = malloc(model_size);
    if (!model_data) {
        LOG_ERROR("Failed to allocate memory for model");
        fclose(fp);
        return false;
    }

    size_t read_size = fread(model_data, 1, model_size, fp);
    fclose(fp);

    if (read_size != model_size) {
        LOG_ERROR("Failed to read model file");
        free(model_data);
        return false;
    }

    // 初始化 RKNN 上下文
    fprintf(stdout, "[DEBUG] Model loaded, size: %zu bytes, initializing RKNN...\n", model_size);
    fflush(stdout);
    int ret = rknn_init(&rknn_ctx_, model_data, model_size, 0, nullptr);
    free(model_data);

    if (ret != RKNN_SUCC) {
        fprintf(stderr, "[ERROR] Failed to init RKNN context: %d\n", ret);
        LOG_ERROR("Failed to init RKNN context: %d", ret);
        return false;
    }

    // 设置 NPU 核心
    rknn_core_mask mask;
    switch (core_mask) {
        case NPU_CORE_0:
            mask = RKNN_NPU_CORE_0;
            break;
        case NPU_CORE_1:
            mask = RKNN_NPU_CORE_1;
            break;
        case NPU_CORE_2:
            mask = RKNN_NPU_CORE_2;
            break;
        default:
            mask = RKNN_NPU_CORE_AUTO;
            break;
    }

    ret = rknn_set_core_mask(rknn_ctx_, mask);
    if (ret != RKNN_SUCC) {
        LOG_WARN("Failed to set NPU core mask: %d", ret);
    }

    // 获取输入输出信息
    rknn_input_output_num io_num;
    ret = rknn_query(rknn_ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret != RKNN_SUCC) {
        LOG_ERROR("Failed to query IO num: %d", ret);
        Release();
        return false;
    }

    n_input_ = io_num.n_input;
    n_output_ = io_num.n_output;

    // 分配输入属性内存
    input_attrs_ = new rknn_tensor_attr[n_input_];
    memset(input_attrs_, 0, sizeof(rknn_tensor_attr) * n_input_);

    for (uint32_t i = 0; i < n_input_; i++) {
        input_attrs_[i].index = i;
        ret = rknn_query(rknn_ctx_, RKNN_QUERY_INPUT_ATTR,
                        &input_attrs_[i],
                        sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            LOG_ERROR("Failed to query input attr: %d", ret);
            Release();
            return false;
        }
    }

    // 分配输出属性内存
    output_attrs_ = new rknn_tensor_attr[n_output_];
    memset(output_attrs_, 0, sizeof(rknn_tensor_attr) * n_output_);

    for (uint32_t i = 0; i < n_output_; i++) {
        output_attrs_[i].index = i;
        ret = rknn_query(rknn_ctx_, RKNN_QUERY_OUTPUT_ATTR,
                        &output_attrs_[i],
                        sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            LOG_ERROR("Failed to query output attr: %d", ret);
            Release();
            return false;
        }
    }

    initialized_ = true;
    LOG_INFO("Model engine initialized: %s", model_path.c_str());
    return true;
}

// ============================================================
// 释放模型资源
// ============================================================
void ModelEngine::Release() {
    if (rknn_ctx_) {
        rknn_destroy(rknn_ctx_);
        rknn_ctx_ = 0;
    }

    if (input_attrs_) {
        delete[] input_attrs_;
        input_attrs_ = nullptr;
    }

    if (output_attrs_) {
        delete[] output_attrs_;
        output_attrs_ = nullptr;
    }

    n_input_ = 0;
    n_output_ = 0;
    initialized_ = false;
}
