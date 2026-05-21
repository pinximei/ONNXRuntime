#include "stdafx.h"
#include "Worker.h"
#include "Resource.h"
#include "image_io.h"

#include <chrono>
#include <thread>

namespace {

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace

Worker::Worker(CWnd* notify, const AssetPaths& paths)
    : notify_(notify)
{
    try {
        det_ = std::make_unique<uid::Detector>(paths.detect_model);
        uid::OcrPipeline::Paths pp;
        pp.cn_model = paths.cn_rec_model;
        pp.cn_dict  = paths.cn_dict;
        pp.en_model = paths.en_rec_model;
        pp.en_dict  = paths.en_dict;
        ocr_ = std::make_unique<uid::OcrPipeline>(pp);
        ready_ = true;
    } catch (const std::exception& e) {
        init_error_ = e.what();
    }
}

void Worker::Run(const std::wstring& image_path) {
    if (!ready_) return;

    HWND hwnd = notify_->GetSafeHwnd();
    uid::Detector*    det = det_.get();
    uid::OcrPipeline* ocr = ocr_.get();

    std::thread([hwnd, det, ocr, image_path]() {
        auto* result = new InferenceResult;
        result->image_path = image_path;
        try {
            int W = 0, H = 0;
            std::string path = narrow(image_path);
            uint8_t* img = uid::load_image_rgb(path, &W, &H);
            if (!img) throw std::runtime_error("Failed to load image");

            result->w = W;
            result->h = H;
            result->rgb.assign(img, img + static_cast<size_t>(W) * H * 3);
            uid::free_image(img);

            auto t0 = std::chrono::steady_clock::now();
            result->boxes = det->detect(result->rgb.data(), W, H);
            auto t1 = std::chrono::steady_clock::now();
            result->texts = ocr->recognize_boxes(result->rgb.data(), W, H, result->boxes);
            auto t2 = std::chrono::steady_clock::now();
            // Drop low-confidence OCR text.
            for (auto& r : result->texts) {
                if (r.score < 0.50f) { r.text.clear(); r.score = 0; }
            }
            result->detect_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            result->ocr_ms    = std::chrono::duration<double, std::milli>(t2 - t1).count();
        } catch (const std::exception& e) {
            result->error = e.what();
        }
        ::PostMessage(hwnd, WM_INFERENCE_DONE, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}
