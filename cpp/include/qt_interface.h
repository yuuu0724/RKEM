#ifndef QT_INTERFACE_H
#define QT_INTERFACE_H

#include "common.h"
#include <string>
#include <functional>

// ============================================================
// QT 界面接口
// ============================================================
// 预留接口，用于后续集成 QT 图形界面
// 提供回调函数注册机制，用于接收检测结果
// ============================================================

class QtInterface {
public:
    // 获取单例
    static QtInterface& GetInstance();

    // 初始化 QT 界面
    bool Init(int argc, char* argv[]);

    // 运行主循环
    int Run();

    // 退出
    void Quit();

    // 注册 OCR 结果回调
    void RegisterOcrCallback(std::function<void(const OcrResult&)> callback);

    // 注册疲劳检测结果回调
    void RegisterFatigueCallback(std::function<void(const FatigueResult&)> callback);

    // 注册缺陷检测结果回调
    void RegisterDefectCallback(std::function<void(const DefectResult&)> callback);

    // 注册状态回调
    void RegisterStatusCallback(std::function<void(const std::string&)> callback);

    // 显示图像
    void ShowImage(const uint8_t* data, int width, int height, int channels);

    // 显示状态信息
    void ShowStatus(const std::string& status);

    // 显示检测结果
    void ShowOcrResult(const OcrResult& result);
    void ShowFatigueResult(const FatigueResult& result);
    void ShowDefectResult(const DefectResult& result);

private:
    QtInterface();
    ~QtInterface();

    QtInterface(const QtInterface&) = delete;
    QtInterface& operator=(const QtInterface&) = delete;

private:
    bool initialized_;
    bool running_;

    // 回调函数
    std::function<void(const OcrResult&)> ocr_callback_;
    std::function<void(const FatigueResult&)> fatigue_callback_;
    std::function<void(const DefectResult&)> defect_callback_;
    std::function<void(const std::string&)> status_callback_;
};

#endif // QT_INTERFACE_H
