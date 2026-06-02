#ifndef YOLO_DETECTOR_H
#define YOLO_DETECTOR_H

#include <rknn_api.h>

#include <opencv2/core.hpp>

#include <string>
#include <vector>

struct YoloDetection {
    int classId = -1;
    float score = 0.0f;
    cv::Rect box;
};

class InspectionYoloDetector {
public:
    InspectionYoloDetector() = default;
    ~InspectionYoloDetector();

    InspectionYoloDetector(const InspectionYoloDetector&) = delete;
    InspectionYoloDetector& operator=(const InspectionYoloDetector&) = delete;

    bool init(const std::string& modelPath,
              const std::string& labelsPath,
              int classCount,
              float threshold,
              rknn_core_mask coreMask);
    void release();

    bool detect(const cv::Mat& gray640, std::vector<YoloDetection>& detections);
    std::string label(int classId) const;

private:
    struct Layout {
        bool channelFirst = true;
        int channels = 0;
        int anchors = 0;
        bool hasObjectness = false;
    };

    struct Candidate {
        float box[4];
        float score = 0.0f;
        int classId = -1;
    };

    static bool isQuantTensor(const rknn_tensor_attr& attr);
    static float sigmoid(float value);
    static float dequantI8(int8_t value, int32_t zeroPoint, float scale);
    static float dequantU8(uint8_t value, int32_t zeroPoint, float scale);
    static float iou(const cv::Rect& a, const cv::Rect& b);
    static void rawBoxToXyxy(const Candidate& candidate,
                             bool xywh,
                             bool normalized,
                             int imageSize,
                             float xyxy[4]);
    static int boxQuality(const std::vector<Candidate>& candidates,
                          bool xywh,
                          bool normalized,
                          int imageSize,
                          float& medianArea);
    static bool chooseXywh(const std::vector<Candidate>& candidates,
                           bool normalized,
                           int imageSize);

    float value(void* output, const rknn_tensor_attr& attr, int offset) const;
    bool getLayout(const rknn_tensor_attr& attr, Layout& layout) const;
    int offset(const Layout& layout, int anchor, int channel) const;
    bool matchOutputShape(const rknn_tensor_attr& attr,
                          int channels,
                          bool& channelFirst,
                          int& anchors) const;
    int twoOutputOffset(bool channelFirst,
                        int anchors,
                        int channels,
                        int anchor,
                        int channel) const;
    void decodeSingleOutput(void* output,
                            const rknn_tensor_attr& attr,
                            std::vector<YoloDetection>& detections);
    void decodeTwoOutputs(const std::vector<rknn_output>& outputs,
                          std::vector<YoloDetection>& detections);

private:
    rknn_context m_context = 0;
    std::vector<rknn_tensor_attr> m_inputAttrs;
    std::vector<rknn_tensor_attr> m_outputAttrs;
    std::vector<std::string> m_labels;
    int m_modelWidth = 640;
    int m_modelHeight = 640;
    int m_modelChannels = 3;
    int m_classCount = 0;
    float m_threshold = 0.15f;
};

#endif // YOLO_DETECTOR_H
