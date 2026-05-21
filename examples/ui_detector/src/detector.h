#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

namespace uid {

struct Box {
    float x;      // top-left, in original image coordinates
    float y;
    float w;
    float h;
    float score;
};

struct DetectorOptions {
    float conf_threshold = 0.05f;   // OmniParser benefits from a low threshold
    float iou_threshold  = 0.50f;
    // Drop boxes that cover more than this fraction of the image. OmniParser
    // sometimes flags an entire sidebar/panel as one "interactive region" —
    // that's not useful for our task.
    float max_area_ratio = 0.25f;
    int   intra_threads  = 0;       // 0 = auto
};

class Detector {
public:
    Detector(const std::wstring& model_path, const DetectorOptions& opts = {});

    // Run on a decoded RGB image (uint8, HxWx3, row-major).
    std::vector<Box> detect(const uint8_t* rgb, int width, int height);

    // Convenience: load image file and detect.
    std::vector<Box> detect_file(const std::string& image_path);

    int input_size() const { return input_size_; }

private:
    Ort::Env env_;
    Ort::SessionOptions opts_;
    std::unique_ptr<Ort::Session> session_;
    Ort::AllocatorWithDefaultOptions allocator_;

    std::string input_name_;
    std::string output_name_;
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;

    DetectorOptions options_;
    int input_size_ = 640;  // square, fixed by the 640x640 OmniParser ONNX
};

} // namespace uid
