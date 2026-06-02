#ifndef HMI_OCR_DETECTOR_H
#define HMI_OCR_DETECTOR_H

#include "../../../chip/cpp/ppocrv5.h"

#include <opencv2/core.hpp>

#include <string>

struct HmiOcrResult {
    std::string rawText;
    std::string normalizedText;
    float bestScore = 0.0f;
    int resultCount = 0;
};

class HmiOcrDetector {
public:
    HmiOcrDetector();
    ~HmiOcrDetector();

    HmiOcrDetector(const HmiOcrDetector&) = delete;
    HmiOcrDetector& operator=(const HmiOcrDetector&) = delete;

    bool init(const std::string& detModelPath,
              const std::string& recModelPath);
    void release();
    bool recognize(const cv::Mat& gray640,
                   double minTextScore,
                   HmiOcrResult& result);

private:
    ppocr_system_app_context m_context;
    bool m_initialized = false;
};

std::string normalizeChipText(const std::string& text);
bool fuzzyChipMatch(const std::string& expected,
                    const std::string& observed,
                    int maxMissingChars = 3);

#endif // HMI_OCR_DETECTOR_H
