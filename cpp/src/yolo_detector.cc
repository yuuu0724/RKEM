#include "yolo_detector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include <opencv2/imgproc.hpp>

namespace {
constexpr int kInputSize = 640;
constexpr float kNmsThreshold = 0.45f;

std::vector<std::string> loadLabels(const std::string& path)
{
    std::vector<std::string> labels;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            labels.push_back(line);
        }
    }
    return labels;
}

unsigned char *loadModel(const std::string& path, int *modelSize)
{
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) {
        std::fprintf(stderr, "[ERROR] fopen %s failed\n", path.c_str());
        return nullptr;
    }
    fseek(fp, 0, SEEK_END);
    const int size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *data = static_cast<unsigned char *>(std::malloc(size));
    if (!data) {
        fclose(fp);
        return nullptr;
    }
    if (static_cast<int>(fread(data, 1, size, fp)) != size) {
        std::free(data);
        fclose(fp);
        return nullptr;
    }
    fclose(fp);
    *modelSize = size;
    return data;
}
}

InspectionYoloDetector::~InspectionYoloDetector()
{
    release();
}

bool InspectionYoloDetector::init(const std::string& modelPath,
                                  const std::string& labelsPath,
                                  int classCount,
                                  float threshold,
                                  rknn_core_mask coreMask)
{
    m_classCount = classCount;
    m_threshold = threshold;
    m_labels = loadLabels(labelsPath);

    int modelSize = 0;
    unsigned char *model = loadModel(modelPath, &modelSize);
    if (!model) {
        return false;
    }

    int ret = rknn_init(&m_context, model, modelSize, 0, nullptr);
    std::free(model);
    if (ret != RKNN_SUCC) {
        std::fprintf(stderr, "[ERROR] rknn_init %s failed: %d\n", modelPath.c_str(), ret);
        return false;
    }

    ret = rknn_set_core_mask(m_context, coreMask);
    if (ret != RKNN_SUCC) {
        std::fprintf(stderr, "[WARN] set core mask failed for %s: %d\n", modelPath.c_str(), ret);
    }

    rknn_input_output_num ioNum;
    ret = rknn_query(m_context, RKNN_QUERY_IN_OUT_NUM, &ioNum, sizeof(ioNum));
    if (ret != RKNN_SUCC) {
        return false;
    }

    m_inputAttrs.resize(ioNum.n_input);
    m_outputAttrs.resize(ioNum.n_output);
    for (uint32_t i = 0; i < ioNum.n_input; ++i) {
        memset(&m_inputAttrs[i], 0, sizeof(rknn_tensor_attr));
        m_inputAttrs[i].index = i;
        rknn_query(m_context, RKNN_QUERY_INPUT_ATTR, &m_inputAttrs[i], sizeof(rknn_tensor_attr));
    }
    for (uint32_t i = 0; i < ioNum.n_output; ++i) {
        memset(&m_outputAttrs[i], 0, sizeof(rknn_tensor_attr));
        m_outputAttrs[i].index = i;
        rknn_query(m_context, RKNN_QUERY_OUTPUT_ATTR, &m_outputAttrs[i], sizeof(rknn_tensor_attr));
    }

    if (m_inputAttrs[0].fmt == RKNN_TENSOR_NCHW) {
        m_modelChannels = m_inputAttrs[0].dims[1];
        m_modelHeight = m_inputAttrs[0].dims[2];
        m_modelWidth = m_inputAttrs[0].dims[3];
    } else {
        m_modelHeight = m_inputAttrs[0].dims[1];
        m_modelWidth = m_inputAttrs[0].dims[2];
        m_modelChannels = m_inputAttrs[0].dims[3];
    }

    std::printf("[INFO] HMI YOLO model loaded: %s input=%dx%dx%d outputs=%zu\n",
                modelPath.c_str(), m_modelWidth, m_modelHeight, m_modelChannels, m_outputAttrs.size());
    std::fflush(stdout);
    return true;
}

void InspectionYoloDetector::release()
{
    if (m_context) {
        rknn_destroy(m_context);
        m_context = 0;
    }
}

std::string InspectionYoloDetector::label(int classId) const
{
    if (classId >= 0 && classId < static_cast<int>(m_labels.size())) {
        return m_labels[classId];
    }
    return "class_" + std::to_string(classId);
}

bool InspectionYoloDetector::detect(const cv::Mat& gray640, std::vector<YoloDetection>& detections)
{
    if (!m_context || gray640.empty()) {
        return false;
    }

    cv::Mat resized;
    if (gray640.cols != m_modelWidth || gray640.rows != m_modelHeight) {
        cv::resize(gray640, resized, cv::Size(m_modelWidth, m_modelHeight));
    } else {
        resized = gray640;
    }

    cv::Mat input;
    if (m_modelChannels == 1) {
        input = resized.isContinuous() ? resized : resized.clone();
    } else {
        cv::cvtColor(resized, input, cv::COLOR_GRAY2RGB);
    }

    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].size = m_modelWidth * m_modelHeight * m_modelChannels;
    inputs[0].buf = input.data;

    int ret = rknn_inputs_set(m_context, 1, inputs);
    if (ret != RKNN_SUCC) {
        std::fprintf(stderr, "[ERROR] rknn_inputs_set failed: %d\n", ret);
        return false;
    }
    ret = rknn_run(m_context, nullptr);
    if (ret != RKNN_SUCC) {
        std::fprintf(stderr, "[ERROR] rknn_run failed: %d\n", ret);
        return false;
    }

    std::vector<rknn_output> outputs(m_outputAttrs.size());
    memset(outputs.data(), 0, outputs.size() * sizeof(rknn_output));
    for (size_t i = 0; i < outputs.size(); ++i) {
        outputs[i].index = i;
        outputs[i].want_float = isQuantTensor(m_outputAttrs[i]) ? 0 : 1;
    }
    ret = rknn_outputs_get(m_context, outputs.size(), outputs.data(), nullptr);
    if (ret != RKNN_SUCC) {
        std::fprintf(stderr, "[ERROR] rknn_outputs_get failed: %d\n", ret);
        return false;
    }

    detections.clear();
    if (outputs.size() == 2 && outputs[0].buf && outputs[1].buf) {
        decodeTwoOutputs(outputs, detections);
    } else if (!outputs.empty() && outputs[0].buf) {
        decodeSingleOutput(outputs[0].buf, m_outputAttrs[0], detections);
    } else {
        std::fprintf(stderr, "[ERROR] YOLO has no valid output buffers\n");
    }
    rknn_outputs_release(m_context, outputs.size(), outputs.data());
    return true;
}

bool InspectionYoloDetector::isQuantTensor(const rknn_tensor_attr& attr)
{
    return attr.type == RKNN_TENSOR_INT8 || attr.type == RKNN_TENSOR_UINT8;
}

float InspectionYoloDetector::sigmoid(float value)
{
    if (value >= 0.0f) {
        const float z = std::exp(-value);
        return 1.0f / (1.0f + z);
    }
    const float z = std::exp(value);
    return z / (1.0f + z);
}

float InspectionYoloDetector::dequantI8(int8_t value, int32_t zeroPoint, float scale)
{
    return (static_cast<float>(value) - static_cast<float>(zeroPoint)) * scale;
}

float InspectionYoloDetector::dequantU8(uint8_t value, int32_t zeroPoint, float scale)
{
    return (static_cast<float>(value) - static_cast<float>(zeroPoint)) * scale;
}

float InspectionYoloDetector::value(void* output, const rknn_tensor_attr& attr, int offset) const
{
    if (attr.type == RKNN_TENSOR_INT8) {
        return dequantI8(static_cast<int8_t *>(output)[offset], attr.zp, attr.scale);
    }
    if (attr.type == RKNN_TENSOR_UINT8) {
        return dequantU8(static_cast<uint8_t *>(output)[offset], attr.zp, attr.scale);
    }
    return static_cast<float *>(output)[offset];
}

bool InspectionYoloDetector::getLayout(const rknn_tensor_attr& attr, Layout& layout) const
{
    const int noObj = m_classCount + 4;
    const int withObj = m_classCount + 5;
    if (attr.n_dims == 3) {
        if (attr.dims[1] == noObj || attr.dims[1] == withObj) {
            layout.channelFirst = true;
            layout.channels = attr.dims[1];
            layout.anchors = attr.n_elems / layout.channels;
            layout.hasObjectness = layout.channels == withObj;
            return true;
        }
        if (attr.dims[2] == noObj || attr.dims[2] == withObj) {
            layout.channelFirst = false;
            layout.channels = attr.dims[2];
            layout.anchors = attr.n_elems / layout.channels;
            layout.hasObjectness = layout.channels == withObj;
            return true;
        }
    }
    if (attr.n_dims == 2) {
        if (attr.dims[0] == noObj || attr.dims[0] == withObj) {
            layout.channelFirst = true;
            layout.channels = attr.dims[0];
            layout.anchors = attr.dims[1];
            layout.hasObjectness = layout.channels == withObj;
            return true;
        }
        if (attr.dims[1] == noObj || attr.dims[1] == withObj) {
            layout.channelFirst = false;
            layout.channels = attr.dims[1];
            layout.anchors = attr.dims[0];
            layout.hasObjectness = layout.channels == withObj;
            return true;
        }
    }
    std::fprintf(stderr, "[ERROR] Unsupported YOLO output shape dims=[%d,%d,%d,%d], n_dims=%d\n",
                 attr.dims[0], attr.dims[1], attr.dims[2], attr.dims[3], attr.n_dims);
    return false;
}

int InspectionYoloDetector::offset(const Layout& layout, int anchor, int channel) const
{
    if (layout.channelFirst) {
        return channel * layout.anchors + anchor;
    }
    return anchor * layout.channels + channel;
}

void InspectionYoloDetector::rawBoxToXyxy(const Candidate& candidate,
                                          bool xywh,
                                          bool normalized,
                                          int imageSize,
                                          float xyxy[4])
{
    float box[4] = {candidate.box[0], candidate.box[1], candidate.box[2], candidate.box[3]};
    if (normalized) {
        for (float& value : box) {
            value *= static_cast<float>(imageSize);
        }
    }
    if (xywh) {
        xyxy[0] = box[0] - box[2] / 2.0f;
        xyxy[1] = box[1] - box[3] / 2.0f;
        xyxy[2] = box[0] + box[2] / 2.0f;
        xyxy[3] = box[1] + box[3] / 2.0f;
    } else {
        xyxy[0] = box[0];
        xyxy[1] = box[1];
        xyxy[2] = box[2];
        xyxy[3] = box[3];
    }
}

int InspectionYoloDetector::boxQuality(const std::vector<Candidate>& candidates,
                                       bool xywh,
                                       bool normalized,
                                       int imageSize,
                                       float& medianArea)
{
    std::vector<float> areas;
    for (const Candidate& candidate : candidates) {
        float box[4];
        rawBoxToXyxy(candidate, xywh, normalized, imageSize, box);
        const float width = box[2] - box[0];
        const float height = box[3] - box[1];
        const float area = width * height;
        const bool intersects = box[2] > 0.0f && box[3] > 0.0f &&
            box[0] < imageSize && box[1] < imageSize;
        if (intersects && width > 2.0f && height > 2.0f &&
            area > 8.0f && area < imageSize * imageSize * 0.95f) {
            areas.push_back(area);
        }
    }
    if (areas.empty()) {
        medianArea = 0.0f;
        return 0;
    }
    std::sort(areas.begin(), areas.end());
    medianArea = areas[areas.size() / 2];
    return static_cast<int>(areas.size());
}

bool InspectionYoloDetector::chooseXywh(const std::vector<Candidate>& candidates,
                                        bool normalized,
                                        int imageSize)
{
    float xywhArea = 0.0f;
    float xyxyArea = 0.0f;
    const int xywhScore = boxQuality(candidates, true, normalized, imageSize, xywhArea);
    const int xyxyScore = boxQuality(candidates, false, normalized, imageSize, xyxyArea);
    if (xyxyScore > xywhScore) {
        return false;
    }
    if (xywhScore > xyxyScore) {
        return true;
    }
    if (xywhArea <= 0.0f || xyxyArea <= 0.0f) {
        return true;
    }
    return !(xyxyArea < xywhArea * 0.35f);
}

float InspectionYoloDetector::iou(const cv::Rect& a, const cv::Rect& b)
{
    const int inter = (a & b).area();
    const int uni = a.area() + b.area() - inter;
    return uni <= 0 ? 0.0f : static_cast<float>(inter) / static_cast<float>(uni);
}

void InspectionYoloDetector::decodeSingleOutput(void* output,
                                                const rknn_tensor_attr& attr,
                                                std::vector<YoloDetection>& detections)
{
    Layout layout;
    if (!getLayout(attr, layout)) {
        return;
    }

    float rawMin = 1.0e30f;
    float rawMax = -1.0e30f;
    float objMin = 1.0e30f;
    float objMax = -1.0e30f;
    for (int i = 0; i < layout.anchors; ++i) {
        if (layout.hasObjectness) {
            const float obj = value(output, attr, offset(layout, i, 4));
            objMin = std::min(objMin, obj);
            objMax = std::max(objMax, obj);
        }
        for (int c = 0; c < m_classCount; ++c) {
            const int scoreChannel = layout.hasObjectness ? 5 + c : 4 + c;
            const float raw = value(output, attr, offset(layout, i, scoreChannel));
            rawMin = std::min(rawMin, raw);
            rawMax = std::max(rawMax, raw);
        }
    }
    const bool sigmoidScore = rawMin < 0.0f || rawMax > 1.0f;
    const bool sigmoidObj = layout.hasObjectness && (objMin < 0.0f || objMax > 1.0f);

    std::vector<Candidate> candidates;
    for (int i = 0; i < layout.anchors; ++i) {
        float bestScore = 0.0f;
        int bestClass = -1;
        for (int c = 0; c < m_classCount; ++c) {
            const int scoreChannel = layout.hasObjectness ? 5 + c : 4 + c;
            const float raw = value(output, attr, offset(layout, i, scoreChannel));
            const float score = sigmoidScore ? sigmoid(raw) : std::max(0.0f, std::min(raw, 1.0f));
            if (score > m_threshold && score > bestScore) {
                bestScore = score;
                bestClass = c;
            }
        }
        if (bestClass < 0) {
            continue;
        }
        if (layout.hasObjectness) {
            const float obj = value(output, attr, offset(layout, i, 4));
            bestScore *= sigmoidObj ? sigmoid(obj) : std::max(0.0f, std::min(obj, 1.0f));
        }
        if (bestScore < m_threshold) {
            continue;
        }

        Candidate candidate;
        for (int k = 0; k < 4; ++k) {
            candidate.box[k] = value(output, attr, offset(layout, i, k));
        }
        candidate.score = bestScore;
        candidate.classId = bestClass;
        candidates.push_back(candidate);
    }
    if (candidates.empty()) {
        return;
    }

    float maxAbs = 0.0f;
    for (const Candidate& candidate : candidates) {
        for (float value : candidate.box) {
            maxAbs = std::max(maxAbs, std::fabs(value));
        }
    }
    const bool normalized = maxAbs <= 2.0f;
    const bool xywh = chooseXywh(candidates, normalized, m_modelWidth);

    std::vector<YoloDetection> decoded;
    for (const Candidate& candidate : candidates) {
        float box[4];
        rawBoxToXyxy(candidate, xywh, normalized, m_modelWidth, box);
        const int x1 = std::max(0, std::min(static_cast<int>(std::round(box[0])), m_modelWidth - 1));
        const int y1 = std::max(0, std::min(static_cast<int>(std::round(box[1])), m_modelHeight - 1));
        const int x2 = std::max(0, std::min(static_cast<int>(std::round(box[2])), m_modelWidth - 1));
        const int y2 = std::max(0, std::min(static_cast<int>(std::round(box[3])), m_modelHeight - 1));
        if (x2 - x1 <= 2 || y2 - y1 <= 2) {
            continue;
        }
        decoded.push_back({candidate.classId, candidate.score, cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2))});
    }

    std::sort(decoded.begin(), decoded.end(), [](const YoloDetection& a, const YoloDetection& b) {
        return a.score > b.score;
    });
    std::vector<bool> removed(decoded.size(), false);
    for (size_t i = 0; i < decoded.size(); ++i) {
        if (removed[i]) {
            continue;
        }
        detections.push_back(decoded[i]);
        for (size_t j = i + 1; j < decoded.size(); ++j) {
            if (!removed[j] && decoded[i].classId == decoded[j].classId &&
                iou(decoded[i].box, decoded[j].box) > kNmsThreshold) {
                removed[j] = true;
            }
        }
    }
}

bool InspectionYoloDetector::matchOutputShape(const rknn_tensor_attr& attr,
                                              int channels,
                                              bool& channelFirst,
                                              int& anchors) const
{
    if (attr.n_dims == 3) {
        if (attr.dims[1] == channels) {
            channelFirst = true;
            anchors = attr.n_elems / channels;
            return anchors > 0;
        }
        if (attr.dims[2] == channels) {
            channelFirst = false;
            anchors = attr.n_elems / channels;
            return anchors > 0;
        }
    }
    if (attr.n_dims == 2) {
        if (attr.dims[0] == channels) {
            channelFirst = true;
            anchors = attr.dims[1];
            return anchors > 0;
        }
        if (attr.dims[1] == channels) {
            channelFirst = false;
            anchors = attr.dims[0];
            return anchors > 0;
        }
    }
    return false;
}

int InspectionYoloDetector::twoOutputOffset(bool channelFirst,
                                            int anchors,
                                            int channels,
                                            int anchor,
                                            int channel) const
{
    return channelFirst ? channel * anchors + anchor : anchor * channels + channel;
}

void InspectionYoloDetector::decodeTwoOutputs(const std::vector<rknn_output>& outputs,
                                              std::vector<YoloDetection>& detections)
{
    int boxIndex = -1;
    int scoreIndex = -1;
    int boxAnchors = 0;
    int scoreAnchors = 0;
    bool boxChannelFirst = true;
    bool scoreChannelFirst = true;

    for (size_t i = 0; i < m_outputAttrs.size(); ++i) {
        int anchors = 0;
        bool channelFirst = true;
        if (matchOutputShape(m_outputAttrs[i], 4, channelFirst, anchors)) {
            boxIndex = static_cast<int>(i);
            boxAnchors = anchors;
            boxChannelFirst = channelFirst;
        }
        if (matchOutputShape(m_outputAttrs[i], m_classCount, channelFirst, anchors)) {
            scoreIndex = static_cast<int>(i);
            scoreAnchors = anchors;
            scoreChannelFirst = channelFirst;
        }
    }

    if (boxIndex < 0 || scoreIndex < 0 || boxAnchors <= 0 || boxAnchors != scoreAnchors) {
        std::fprintf(stderr, "[ERROR] unsupported two-output YOLO shape\n");
        return;
    }

    void *boxOutput = outputs[boxIndex].buf;
    void *scoreOutput = outputs[scoreIndex].buf;
    const rknn_tensor_attr& boxAttr = m_outputAttrs[boxIndex];
    const rknn_tensor_attr& scoreAttr = m_outputAttrs[scoreIndex];
    const int anchors = boxAnchors;

    float rawMin = 1.0e30f;
    float rawMax = -1.0e30f;
    for (int i = 0; i < anchors; ++i) {
        for (int c = 0; c < m_classCount; ++c) {
            const int off = twoOutputOffset(scoreChannelFirst, anchors, m_classCount, i, c);
            const float raw = value(scoreOutput, scoreAttr, off);
            rawMin = std::min(rawMin, raw);
            rawMax = std::max(rawMax, raw);
        }
    }
    const bool sigmoidScore = rawMin < 0.0f || rawMax > 1.0f;

    std::vector<YoloDetection> decoded;
    for (int i = 0; i < anchors; ++i) {
        float bestScore = 0.0f;
        int bestClass = -1;
        for (int c = 0; c < m_classCount; ++c) {
            const int off = twoOutputOffset(scoreChannelFirst, anchors, m_classCount, i, c);
            const float raw = value(scoreOutput, scoreAttr, off);
            const float score = sigmoidScore ? sigmoid(raw) : std::max(0.0f, std::min(raw, 1.0f));
            if (score > m_threshold && score > bestScore) {
                bestScore = score;
                bestClass = c;
            }
        }
        if (bestClass < 0) {
            continue;
        }

        float cx = value(boxOutput, boxAttr, twoOutputOffset(boxChannelFirst, anchors, 4, i, 0));
        float cy = value(boxOutput, boxAttr, twoOutputOffset(boxChannelFirst, anchors, 4, i, 1));
        float width = value(boxOutput, boxAttr, twoOutputOffset(boxChannelFirst, anchors, 4, i, 2));
        float height = value(boxOutput, boxAttr, twoOutputOffset(boxChannelFirst, anchors, 4, i, 3));
        if (std::max(std::max(std::fabs(cx), std::fabs(cy)), std::max(std::fabs(width), std::fabs(height))) <= 2.0f) {
            cx *= m_modelWidth;
            width *= m_modelWidth;
            cy *= m_modelHeight;
            height *= m_modelHeight;
        }

        const int x1 = std::max(0, std::min(static_cast<int>(std::round(cx - width / 2.0f)), m_modelWidth - 1));
        const int y1 = std::max(0, std::min(static_cast<int>(std::round(cy - height / 2.0f)), m_modelHeight - 1));
        const int x2 = std::max(0, std::min(static_cast<int>(std::round(cx + width / 2.0f)), m_modelWidth - 1));
        const int y2 = std::max(0, std::min(static_cast<int>(std::round(cy + height / 2.0f)), m_modelHeight - 1));
        if (x2 - x1 <= 2 || y2 - y1 <= 2) {
            continue;
        }
        decoded.push_back({bestClass, bestScore, cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2))});
    }

    std::sort(decoded.begin(), decoded.end(), [](const YoloDetection& a, const YoloDetection& b) {
        return a.score > b.score;
    });
    std::vector<bool> removed(decoded.size(), false);
    for (size_t i = 0; i < decoded.size(); ++i) {
        if (removed[i]) {
            continue;
        }
        detections.push_back(decoded[i]);
        for (size_t j = i + 1; j < decoded.size(); ++j) {
            if (!removed[j] && decoded[i].classId == decoded[j].classId &&
                iou(decoded[i].box, decoded[j].box) > kNmsThreshold) {
                removed[j] = true;
            }
        }
    }
}
