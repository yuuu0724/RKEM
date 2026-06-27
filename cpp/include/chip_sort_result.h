#ifndef CHIP_SORT_RESULT_H
#define CHIP_SORT_RESULT_H

#include <string>

struct ChipSortResult {
    int slot_index = 0;
    bool good = false;
    std::string chip_model;
    std::string ocr_text;
    std::string defect_summary;
    std::string reason;
};

#endif // CHIP_SORT_RESULT_H
