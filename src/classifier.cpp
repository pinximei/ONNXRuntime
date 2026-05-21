#include "classifier.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace cls {

namespace {

// Bilinear resize from arbitrary HxW RGB uint8 → dst_h x dst_w RGB float (uint8 range).
void resize_bilinear_rgb(const uint8_t* src, int sw, int sh,
                         float* dst, int dw, int dh)
{
    const float sx = static_cast<float>(sw) / dw;
    const float sy = static_cast<float>(sh) / dh;

    for (int y = 0; y < dh; ++y) {
        float fy = (y + 0.5f) * sy - 0.5f;
        int   y0 = static_cast<int>(std::floor(fy));
        float wy = fy - y0;
        int   y1 = y0 + 1;
        y0 = std::clamp(y0, 0, sh - 1);
        y1 = std::clamp(y1, 0, sh - 1);

        for (int x = 0; x < dw; ++x) {
            float fx = (x + 0.5f) * sx - 0.5f;
            int   x0 = static_cast<int>(std::floor(fx));
            float wx = fx - x0;
            int   x1 = x0 + 1;
            x0 = std::clamp(x0, 0, sw - 1);
            x1 = std::clamp(x1, 0, sw - 1);

            const uint8_t* p00 = src + (y0 * sw + x0) * 3;
            const uint8_t* p01 = src + (y0 * sw + x1) * 3;
            const uint8_t* p10 = src + (y1 * sw + x0) * 3;
            const uint8_t* p11 = src + (y1 * sw + x1) * 3;

            float* o = dst + (y * dw + x) * 3;
            for (int c = 0; c < 3; ++c) {
                float a = p00[c] * (1 - wx) + p01[c] * wx;
                float b = p10[c] * (1 - wx) + p11[c] * wx;
                o[c]    = a * (1 - wy) + b * wy;
            }
        }
    }
}

// EfficientNet-Lite4 preprocessing per onnx/models reference (pre_process_edgetpu):
//   1. proportional resize so the short side becomes 224 / 0.875 = 256
//   2. center-crop 224x224
//   3. (x - 127) / 128  →  approx [-1, 1]
// Returns NHWC float buffer of size out_h*out_w*3.
std::vector<float> preprocess_edgetpu(const uint8_t* src, int sw, int sh,
                                      int out_h, int out_w)
{
    constexpr float kScalePct = 87.5f; // EfficientNet-Lite resize scale
    int new_h = static_cast<int>(100.f * out_h / kScalePct);
    int new_w = static_cast<int>(100.f * out_w / kScalePct);
    int resize_h, resize_w;
    if (sh > sw) {
        resize_w = new_w;
        resize_h = static_cast<int>(static_cast<float>(new_h) * sh / sw);
    } else {
        resize_h = new_h;
        resize_w = static_cast<int>(static_cast<float>(new_w) * sw / sh);
    }

    std::vector<float> resized(static_cast<size_t>(resize_h) * resize_w * 3);
    resize_bilinear_rgb(src, sw, sh, resized.data(), resize_w, resize_h);

    int left = (resize_w - out_w) / 2;
    int top  = (resize_h - out_h) / 2;

    std::vector<float> out(static_cast<size_t>(out_h) * out_w * 3);
    for (int y = 0; y < out_h; ++y) {
        const float* src_row = resized.data() + ((top + y) * resize_w + left) * 3;
        float* dst_row = out.data() + y * out_w * 3;
        for (int x = 0; x < out_w * 3; ++x) {
            dst_row[x] = (src_row[x] - 127.0f) / 128.0f;
        }
    }
    return out;
}

} // namespace

Classifier::Classifier(const std::wstring& model_path, int intra_threads)
    : env_(ORT_LOGGING_LEVEL_WARNING, "classifier")
{
    opts_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    if (intra_threads > 0) {
        opts_.SetIntraOpNumThreads(intra_threads);
    }
    opts_.SetExecutionMode(ORT_SEQUENTIAL);

    session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), opts_);

    auto in_name_ptr  = session_->GetInputNameAllocated(0, allocator_);
    auto out_name_ptr = session_->GetOutputNameAllocated(0, allocator_);
    input_name_  = in_name_ptr.get();
    output_name_ = out_name_ptr.get();
    input_names_  = { input_name_.c_str() };
    output_names_ = { output_name_.c_str() };

    auto type_info = session_->GetInputTypeInfo(0);
    auto tinfo     = type_info.GetTensorTypeAndShapeInfo();
    auto shape     = tinfo.GetShape();
    // EfficientNet-Lite4 official ONNX is NHWC: [1, 224, 224, 3].
    // Be defensive and accept NCHW too in case someone swaps in a different model.
    if (shape.size() == 4) {
        if (shape[3] == 3) {
            nhwc_   = true;
            input_h_ = static_cast<int>(shape[1] > 0 ? shape[1] : 224);
            input_w_ = static_cast<int>(shape[2] > 0 ? shape[2] : 224);
        } else if (shape[1] == 3) {
            nhwc_   = false;
            input_h_ = static_cast<int>(shape[2] > 0 ? shape[2] : 224);
            input_w_ = static_cast<int>(shape[3] > 0 ? shape[3] : 224);
        }
    }
}

std::vector<Prediction> Classifier::classify(const uint8_t* rgb,
                                             int width, int height, int top_k)
{
    const int H = input_h_;
    const int W = input_w_;

    std::vector<float> nhwc = preprocess_edgetpu(rgb, width, height, H, W);

    std::vector<float> input_tensor_values;
    if (nhwc_) {
        input_tensor_values = std::move(nhwc);
    } else {
        // Convert NHWC → NCHW for non-standard models.
        input_tensor_values.resize(nhwc.size());
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                for (int c = 0; c < 3; ++c) {
                    input_tensor_values[(c * H + y) * W + x] = nhwc[(y * W + x) * 3 + c];
                }
            }
        }
    }

    std::array<int64_t, 4> in_shape = nhwc_
        ? std::array<int64_t, 4>{1, H, W, 3}
        : std::array<int64_t, 4>{1, 3, H, W};

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input = Ort::Value::CreateTensor<float>(
        mem,
        input_tensor_values.data(),
        input_tensor_values.size(),
        in_shape.data(),
        in_shape.size());

    auto outputs = session_->Run(Ort::RunOptions{nullptr},
                                 input_names_.data(), &input, 1,
                                 output_names_.data(), 1);

    auto& out = outputs.front();
    auto  info = out.GetTensorTypeAndShapeInfo();
    size_t n   = info.GetElementCount();
    const float* scores = out.GetTensorData<float>();

    // EfficientNet-Lite4's output node is "Softmax:0" — the graph already
    // applies softmax, so scores[i] is a probability in [0,1] summing to 1.
    int k = std::min<int>(top_k, static_cast<int>(n));
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::partial_sort(idx.begin(), idx.begin() + k, idx.end(),
        [&](int a, int b) { return scores[a] > scores[b]; });

    std::vector<Prediction> result;
    result.reserve(k);
    for (int i = 0; i < k; ++i) {
        int id = idx[i];
        result.push_back({ id, scores[id], imagenet_label(id) });
    }
    return result;
}

std::vector<Prediction> Classifier::classify_file(const std::string& image_path,
                                                  int top_k)
{
    int w, h, ch;
    uint8_t* data = stbi_load(image_path.c_str(), &w, &h, &ch, 3);
    if (!data) {
        throw std::runtime_error("Failed to load image: " + image_path +
                                 " (" + (stbi_failure_reason() ? stbi_failure_reason() : "unknown") + ")");
    }
    std::vector<Prediction> out;
    try {
        out = classify(data, w, h, top_k);
    } catch (...) {
        stbi_image_free(data);
        throw;
    }
    stbi_image_free(data);
    return out;
}

} // namespace cls
