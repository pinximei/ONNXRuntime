#include "detector.h"
#include "image_io.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace uid {

namespace {

struct Letterbox {
    int   pad_x;
    int   pad_y;
    float scale;
};

// YOLOv8-style letterbox: scale uniformly so the longer side equals input_size,
// then pad with gray (114) to make a square. Writes float32 NCHW [1,3,S,S] in [0,1].
Letterbox letterbox_nchw(const uint8_t* src, int sw, int sh,
                         float* dst, int input_size)
{
    float scale = std::min(static_cast<float>(input_size) / sw,
                           static_cast<float>(input_size) / sh);
    int new_w = static_cast<int>(std::round(sw * scale));
    int new_h = static_cast<int>(std::round(sh * scale));
    int pad_x = (input_size - new_w) / 2;
    int pad_y = (input_size - new_h) / 2;

    // Fill the full destination with 114/255 (gray).
    const size_t plane = static_cast<size_t>(input_size) * input_size;
    const float gray = 114.0f / 255.0f;
    std::fill_n(dst, plane * 3, gray);

    // Bilinear resize directly into the centered region, written as planar CHW.
    const float sx = static_cast<float>(sw) / new_w;
    const float sy = static_cast<float>(sh) / new_h;
    for (int y = 0; y < new_h; ++y) {
        float fy = (y + 0.5f) * sy - 0.5f;
        int   y0 = static_cast<int>(std::floor(fy));
        float wy = fy - y0;
        int   y1 = y0 + 1;
        y0 = std::clamp(y0, 0, sh - 1);
        y1 = std::clamp(y1, 0, sh - 1);

        for (int x = 0; x < new_w; ++x) {
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

            const int dy = pad_y + y;
            const int dx = pad_x + x;
            for (int c = 0; c < 3; ++c) {
                float a = p00[c] * (1 - wx) + p01[c] * wx;
                float b = p10[c] * (1 - wx) + p11[c] * wx;
                float v = a * (1 - wy) + b * wy;
                dst[c * plane + dy * input_size + dx] = v / 255.0f;
            }
        }
    }
    return { pad_x, pad_y, scale };
}

// Greedy NMS on axis-aligned boxes (x,y,w,h,score).
std::vector<Box> nms(std::vector<Box> boxes, float iou_thr) {
    std::sort(boxes.begin(), boxes.end(),
              [](const Box& a, const Box& b) { return a.score > b.score; });
    std::vector<Box> kept;
    std::vector<char> dead(boxes.size(), 0);
    for (size_t i = 0; i < boxes.size(); ++i) {
        if (dead[i]) continue;
        kept.push_back(boxes[i]);
        const float ax1 = boxes[i].x, ay1 = boxes[i].y;
        const float ax2 = ax1 + boxes[i].w, ay2 = ay1 + boxes[i].h;
        const float aa  = boxes[i].w * boxes[i].h;
        for (size_t j = i + 1; j < boxes.size(); ++j) {
            if (dead[j]) continue;
            const float bx1 = boxes[j].x, by1 = boxes[j].y;
            const float bx2 = bx1 + boxes[j].w, by2 = by1 + boxes[j].h;
            const float ix1 = std::max(ax1, bx1);
            const float iy1 = std::max(ay1, by1);
            const float ix2 = std::min(ax2, bx2);
            const float iy2 = std::min(ay2, by2);
            const float iw  = std::max(0.f, ix2 - ix1);
            const float ih  = std::max(0.f, iy2 - iy1);
            const float inter = iw * ih;
            const float bb = boxes[j].w * boxes[j].h;
            const float un = aa + bb - inter;
            if (un > 0 && inter / un > iou_thr) dead[j] = 1;
        }
    }
    return kept;
}

} // namespace

Detector::Detector(const std::wstring& model_path, const DetectorOptions& options)
    : env_(ORT_LOGGING_LEVEL_WARNING, "ui_detector"), options_(options)
{
    opts_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    if (options.intra_threads > 0) opts_.SetIntraOpNumThreads(options.intra_threads);
    opts_.SetExecutionMode(ORT_SEQUENTIAL);

    session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), opts_);

    auto in_name  = session_->GetInputNameAllocated(0, allocator_);
    auto out_name = session_->GetOutputNameAllocated(0, allocator_);
    input_name_  = in_name.get();
    output_name_ = out_name.get();
    input_names_  = { input_name_.c_str() };
    output_names_ = { output_name_.c_str() };

    auto shape = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    // Expected: [1, 3, 640, 640]
    if (shape.size() == 4 && shape[2] > 0) input_size_ = static_cast<int>(shape[2]);
}

std::vector<Box> Detector::detect(const uint8_t* rgb, int width, int height) {
    const int S = input_size_;
    std::vector<float> input(static_cast<size_t>(S) * S * 3);
    Letterbox lb = letterbox_nchw(rgb, width, height, input.data(), S);

    std::array<int64_t, 4> in_shape{1, 3, S, S};
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        mem, input.data(), input.size(), in_shape.data(), in_shape.size());

    auto outputs = session_->Run(Ort::RunOptions{nullptr},
                                 input_names_.data(), &input_tensor, 1,
                                 output_names_.data(), 1);

    // Output shape: [1, 5, 8400] -- channels-first transposed layout typical of YOLOv8 ONNX.
    auto& out  = outputs.front();
    auto  info = out.GetTensorTypeAndShapeInfo();
    auto  oshape = info.GetShape();
    if (oshape.size() != 3 || oshape[0] != 1 || oshape[1] < 5) {
        throw std::runtime_error("Unexpected output shape from OmniParser ONNX");
    }
    const int C = static_cast<int>(oshape[1]);  // 5 (cx, cy, w, h, score)
    const int N = static_cast<int>(oshape[2]);  // 8400
    const float* p = out.GetTensorData<float>();

    auto at = [&](int channel, int idx) { return p[channel * N + idx]; };

    std::vector<Box> raw;
    raw.reserve(256);
    for (int i = 0; i < N; ++i) {
        float score = at(4, i);
        // Some YOLOv8 ONNX have multi-class scores; if so, take the max over channels [4..C).
        for (int c = 5; c < C; ++c) score = std::max(score, at(c, i));
        if (score < options_.conf_threshold) continue;

        float cx = at(0, i);
        float cy = at(1, i);
        float bw = at(2, i);
        float bh = at(3, i);
        // YOLOv8 outputs are in model-input pixel space (0..S).
        float x = cx - bw * 0.5f;
        float y = cy - bh * 0.5f;

        // Undo letterbox to map back to original image coordinates.
        x = (x - lb.pad_x) / lb.scale;
        y = (y - lb.pad_y) / lb.scale;
        bw /= lb.scale;
        bh /= lb.scale;

        // Clip to image bounds.
        float x2 = std::clamp(x + bw, 0.f, static_cast<float>(width));
        float y2 = std::clamp(y + bh, 0.f, static_cast<float>(height));
        x = std::clamp(x, 0.f, static_cast<float>(width));
        y = std::clamp(y, 0.f, static_cast<float>(height));
        bw = std::max(0.f, x2 - x);
        bh = std::max(0.f, y2 - y);
        if (bw < 2 || bh < 2) continue;
        raw.push_back({ x, y, bw, bh, score });
    }
    auto kept = nms(std::move(raw), options_.iou_threshold);

    if (options_.max_area_ratio > 0.f && options_.max_area_ratio < 1.f) {
        const float max_area = options_.max_area_ratio *
                               static_cast<float>(width) * static_cast<float>(height);
        kept.erase(std::remove_if(kept.begin(), kept.end(),
            [max_area](const Box& b) { return b.w * b.h > max_area; }),
            kept.end());
    }
    return kept;
}

std::vector<Box> Detector::detect_file(const std::string& image_path) {
    int w = 0, h = 0;
    uint8_t* data = load_image_rgb(image_path, &w, &h);
    if (!data) throw std::runtime_error("Failed to load image: " + image_path);
    std::vector<Box> boxes;
    try { boxes = detect(data, w, h); }
    catch (...) { free_image(data); throw; }
    free_image(data);
    return boxes;
}

} // namespace uid
