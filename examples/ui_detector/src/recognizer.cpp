#include "recognizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace uid {

namespace {

// Bilinear resize RGB uint8 -> float (still in 0..255 range, normalized later).
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

} // namespace

Recognizer::Recognizer(const std::wstring& model_path,
                       const std::string& dict_path,
                       int intra_threads)
    : env_(ORT_LOGGING_LEVEL_WARNING, "ui_recognizer")
{
    opts_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    if (intra_threads > 0) opts_.SetIntraOpNumThreads(intra_threads);
    opts_.SetExecutionMode(ORT_SEQUENTIAL);

    session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), opts_);

    auto in_name  = session_->GetInputNameAllocated(0, allocator_);
    auto out_name = session_->GetOutputNameAllocated(0, allocator_);
    input_name_  = in_name.get();
    output_name_ = out_name.get();
    input_names_  = { input_name_.c_str() };
    output_names_ = { output_name_.c_str() };

    // Load dictionary. CTC blank takes index 0; PaddleOCR appends a trailing
    // space character at the end of the table.
    std::ifstream f(dict_path);
    if (!f) throw std::runtime_error("Failed to open dict: " + dict_path);
    character_.push_back("");  // index 0 = CTC blank
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        character_.push_back(line);
    }
    character_.push_back(" ");  // trailing space, per PaddleOCR convention
    // Expected size now: 1 + 6623 + 1 = 6625, matching the model output dim.
}

RecResult Recognizer::recognize(const uint8_t* rgb, int width, int height) {
    if (width <= 0 || height <= 0) return { "", 0.f };

    // Width is scaled proportionally to keep aspect ratio,
    // then rounded up to the next multiple of 8 (the model's stride along W).
    float ratio = static_cast<float>(width) / height;
    int new_w  = std::max(8, static_cast<int>(std::ceil(kInH * ratio / 8.0f)) * 8);
    // Cap maximum width to keep memory bounded.
    new_w = std::min(new_w, 1600);

    const size_t plane = static_cast<size_t>(kInH) * new_w;
    std::vector<float> hwc(plane * 3);
    resize_bilinear_rgb(rgb, width, height, hwc.data(), new_w, kInH);

    // HWC -> CHW with normalization (x/255 - 0.5) / 0.5 = x/127.5 - 1.
    std::vector<float> chw(plane * 3);
    for (int y = 0; y < kInH; ++y) {
        for (int x = 0; x < new_w; ++x) {
            for (int c = 0; c < 3; ++c) {
                float v = hwc[(y * new_w + x) * 3 + c] / 127.5f - 1.0f;
                chw[c * plane + y * new_w + x] = v;
            }
        }
    }

    std::array<int64_t, 4> in_shape{1, 3, kInH, new_w};
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input = Ort::Value::CreateTensor<float>(
        mem, chw.data(), chw.size(), in_shape.data(), in_shape.size());

    auto outputs = session_->Run(Ort::RunOptions{nullptr},
                                 input_names_.data(), &input, 1,
                                 output_names_.data(), 1);

    auto& out  = outputs.front();
    auto  info = out.GetTensorTypeAndShapeInfo();
    auto  shape = info.GetShape();
    if (shape.size() != 3) return { "", 0.f };
    const int T = static_cast<int>(shape[1]);
    const int V = static_cast<int>(shape[2]);
    const float* p = out.GetTensorData<float>();

    // CTC greedy decode: at each timestep pick argmax; collapse consecutive
    // identical indices; drop the blank class (index 0).
    std::string text;
    float score_sum = 0.f;
    int   score_n   = 0;
    int   prev = -1;
    for (int t = 0; t < T; ++t) {
        const float* row = p + t * V;
        int   best = 0;
        float bestv = row[0];
        for (int v = 1; v < V; ++v) {
            if (row[v] > bestv) { bestv = row[v]; best = v; }
        }
        if (best != 0 && best != prev) {
            if (best < static_cast<int>(character_.size())) {
                text += character_[best];
                score_sum += bestv;
                ++score_n;
            }
        }
        prev = best;
    }

    float score = (score_n > 0) ? (score_sum / score_n) : 0.f;
    return { text, score };
}

std::vector<RecResult> Recognizer::recognize_boxes(const uint8_t* img_rgb,
                                                   int img_w, int img_h,
                                                   const std::vector<Box>& boxes,
                                                   int pad_px)
{
    std::vector<RecResult> out;
    out.reserve(boxes.size());
    for (const auto& b : boxes) {
        int x0 = std::max(0, static_cast<int>(b.x) - pad_px);
        int y0 = std::max(0, static_cast<int>(b.y) - pad_px);
        int x1 = std::min(img_w, static_cast<int>(b.x + b.w) + pad_px);
        int y1 = std::min(img_h, static_cast<int>(b.y + b.h) + pad_px);
        int cw = x1 - x0;
        int ch = y1 - y0;
        if (cw < 4 || ch < 4) { out.push_back({ "", 0.f }); continue; }

        // Copy the crop into a contiguous buffer.
        std::vector<uint8_t> crop(static_cast<size_t>(cw) * ch * 3);
        for (int y = 0; y < ch; ++y) {
            const uint8_t* src = img_rgb + ((y0 + y) * img_w + x0) * 3;
            std::copy_n(src, static_cast<size_t>(cw) * 3, crop.data() + static_cast<size_t>(y) * cw * 3);
        }
        out.push_back(recognize(crop.data(), cw, ch));
    }
    return out;
}

} // namespace uid
