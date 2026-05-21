#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

namespace cls {

struct Prediction {
    int         class_id;
    float       score;
    std::string label;
};

class Classifier {
public:
    // model_path : path to EfficientNet-Lite4 .onnx
    // intra_threads : 0 lets ORT pick (= number of physical cores)
    Classifier(const std::wstring& model_path, int intra_threads = 0);

    // Run inference on a decoded RGB image (uint8, HxWx3, row-major).
    // Returns top_k predictions sorted by descending score.
    std::vector<Prediction> classify(const uint8_t* rgb,
                                     int width,
                                     int height,
                                     int top_k = 5);

    // Convenience: load image file (jpg/png/bmp/...) and classify.
    std::vector<Prediction> classify_file(const std::string& image_path,
                                          int top_k = 5);

    // Model input spec (read from the loaded graph).
    int input_h() const { return input_h_; }
    int input_w() const { return input_w_; }

private:
    Ort::Env                     env_;
    Ort::SessionOptions          opts_;
    std::unique_ptr<Ort::Session> session_;
    Ort::AllocatorWithDefaultOptions allocator_;

    std::string input_name_;
    std::string output_name_;
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;

    // Cached input shape. EfficientNet-Lite4 is NHWC 1x224x224x3.
    bool nhwc_ = true;
    int  input_h_ = 224;
    int  input_w_ = 224;
};

// Provided by imagenet_labels.cpp (size == 1000).
const char* imagenet_label(int class_id);

} // namespace cls
