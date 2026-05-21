#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "detector.h"  // for uid::Box

namespace uid {

struct RecResult {
    std::string text;
    float       score;   // mean per-step softmax probability of the chosen tokens
};

class Recognizer {
public:
    // model_path : PP-OCRv4 rec ONNX
    // dict_path  : ppocr_keys_v1.txt (one character per line, UTF-8)
    Recognizer(const std::wstring& model_path,
               const std::string& dict_path,
               int intra_threads = 0);

    // Recognize text inside a single crop (uint8 RGB, HxWx3 row-major).
    RecResult recognize(const uint8_t* rgb, int width, int height);

    // Recognize text for each detection box on the original image.
    // Pads each box by `pad_px` to avoid clipping characters at the edge.
    std::vector<RecResult> recognize_boxes(const uint8_t* img_rgb,
                                           int img_w, int img_h,
                                           const std::vector<Box>& boxes,
                                           int pad_px = 2);

private:
    Ort::Env env_;
    Ort::SessionOptions opts_;
    std::unique_ptr<Ort::Session> session_;
    Ort::AllocatorWithDefaultOptions allocator_;

    std::string input_name_;
    std::string output_name_;
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;

    // CTC character table. character_[0] is blank, character_[i>=1] = dict line (i-1).
    std::vector<std::string> character_;

    // Fixed input height for PP-OCRv4 rec.
    static constexpr int kInH = 48;
};

} // namespace uid
