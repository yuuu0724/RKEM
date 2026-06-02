#include "hmi_ocr_detector.h"

#include <rknn_api.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#include <opencv2/imgproc.hpp>

HmiOcrDetector::HmiOcrDetector()
{
    memset(&m_context, 0, sizeof(m_context));
}

HmiOcrDetector::~HmiOcrDetector()
{
    release();
}

bool HmiOcrDetector::init(const std::string& detModelPath,
                          const std::string& recModelPath)
{
    if (m_initialized) {
        return true;
    }

    if (init_ppocr_model(detModelPath.c_str(), &m_context.det_context) != 0 ||
        init_ppocr_rec_model(recModelPath.c_str(), &m_context.rec_context) != 0) {
        release();
        return false;
    }

    rknn_set_core_mask(m_context.det_context.rknn_ctx, static_cast<rknn_core_mask>(RKNN_NPU_CORE_0));
    rknn_set_core_mask(m_context.rec_context.rknn_ctx, static_cast<rknn_core_mask>(RKNN_NPU_CORE_0));
    m_initialized = true;
    return true;
}

void HmiOcrDetector::release()
{
    if (m_context.det_context.rknn_ctx) {
        release_ppocr_model(&m_context.det_context);
    }
    if (m_context.rec_context.rknn_ctx) {
        release_ppocr_model(&m_context.rec_context);
    }
    memset(&m_context, 0, sizeof(m_context));
    m_initialized = false;
}

bool HmiOcrDetector::recognize(const cv::Mat& gray640,
                               double minTextScore,
                               HmiOcrResult& result)
{
    if (!m_initialized || gray640.empty()) {
        return false;
    }

    cv::Mat rgb;
    if (gray640.channels() == 1) {
        cv::cvtColor(gray640, rgb, cv::COLOR_GRAY2RGB);
    } else if (gray640.channels() == 3) {
        rgb = gray640;
    } else {
        return false;
    }

    image_buffer_t image;
    memset(&image, 0, sizeof(image));
    image.width = rgb.cols;
    image.height = rgb.rows;
    image.width_stride = rgb.cols;
    image.height_stride = rgb.rows;
    image.format = IMAGE_FORMAT_RGB888;
    image.virt_addr = rgb.data;
    image.size = rgb.cols * rgb.rows * 3;

    ppocr_det_postprocess_params params;
    params.threshold = 0.3f;
    params.box_threshold = 0.6f;
    params.use_dilate = false;
    params.db_score_mode = const_cast<char*>("slow");
    params.db_box_type = const_cast<char*>("poly");
    params.db_unclip_ratio = 2.0f;

    ppocr_text_recog_array_result_t ocrResults;
    memset(&ocrResults, 0, sizeof(ocrResults));
    if (inference_ppocrv5_model(&m_context, &image, &params, &ocrResults) != 0) {
        return false;
    }

    HmiOcrResult next;
    next.resultCount = ocrResults.count;
    const float minScore = static_cast<float>(std::max(0.0, minTextScore));
    for (int i = 0; i < ocrResults.count; ++i) {
        const ppocr_rec_result& text = ocrResults.text_result[i].text;
        next.bestScore = std::max(next.bestScore, text.score);
        if (text.score >= minScore && text.str[0] != '\0') {
            next.rawText += text.str;
        }
    }
    next.normalizedText = normalizeChipText(next.rawText);
    result = next;
    return true;
}

std::string normalizeChipText(const std::string& text)
{
    std::string normalized;
    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::toupper(ch)));
        }
    }
    return normalized;
}

bool fuzzyChipMatch(const std::string& expected,
                    const std::string& observed,
                    int maxMissingChars)
{
    const std::string normalizedExpected = normalizeChipText(expected);
    const std::string normalizedObserved = normalizeChipText(observed);
    if (normalizedExpected.empty() || normalizedObserved.empty()) {
        return false;
    }
    if (normalizedObserved.find(normalizedExpected) != std::string::npos) {
        return true;
    }

    std::vector<int> dp(normalizedObserved.size() + 1, 0);
    for (char expectedChar : normalizedExpected) {
        int previous = 0;
        for (size_t j = 1; j <= normalizedObserved.size(); ++j) {
            const int saved = dp[j];
            if (expectedChar == normalizedObserved[j - 1]) {
                dp[j] = previous + 1;
            } else {
                dp[j] = std::max(dp[j], dp[j - 1]);
            }
            previous = saved;
        }
    }

    return static_cast<int>(normalizedExpected.size()) - dp[normalizedObserved.size()] <= maxMissingChars;
}
