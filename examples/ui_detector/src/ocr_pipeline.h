#pragma once

#include "detector.h"
#include "recognizer.h"

#include <memory>
#include <string>

namespace uid {

// Two-model OCR with per-box selection: tries both Chinese and English recognizers,
// then chooses the better result based on character composition and confidence.
class OcrPipeline {
public:
    struct Paths {
        std::wstring cn_model;
        std::string  cn_dict;
        std::wstring en_model;
        std::string  en_dict;
    };

    OcrPipeline(const Paths& p, int intra_threads = 0);

    // Recognize text for each detection box. Applies geometric filters first
    // (drop boxes that are too small, too thin, etc.), then runs both
    // recognizers on the survivors and picks the better result.
    std::vector<RecResult> recognize_boxes(const uint8_t* img_rgb,
                                           int img_w, int img_h,
                                           const std::vector<Box>& boxes,
                                           int pad_px = 2);

private:
    std::unique_ptr<Recognizer> cn_;
    std::unique_ptr<Recognizer> en_;
};

} // namespace uid
