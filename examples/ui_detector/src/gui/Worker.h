#pragma once
#include "stdafx.h"
#include "detector.h"
#include "ocr_pipeline.h"

struct InferenceResult {
    std::vector<unsigned char>       rgb;   // decoded image
    int                              w = 0;
    int                              h = 0;
    std::vector<uid::Box>            boxes;
    std::vector<uid::RecResult>      texts;
    std::wstring                     image_path;
    std::string                      error;
    double                           detect_ms = 0;
    double                           ocr_ms    = 0;
};

// Owns Detector + Recognizer; runs Run() on a worker thread that posts
// WM_INFERENCE_DONE to `notify` when done. The lParam is a heap-allocated
// InferenceResult* that the UI thread takes ownership of.
class Worker {
public:
    struct AssetPaths {
        std::wstring detect_model;
        std::wstring cn_rec_model;
        std::string  cn_dict;
        std::wstring en_rec_model;
        std::string  en_dict;
    };
    Worker(CWnd* notify, const AssetPaths& paths);

    bool ready() const { return ready_; }
    const std::string& init_error() const { return init_error_; }

    // Starts a background thread; immediately returns.
    void Run(const std::wstring& image_path);

private:
    CWnd* notify_ = nullptr;
    std::unique_ptr<uid::Detector>    det_;
    std::unique_ptr<uid::OcrPipeline> ocr_;
    bool                              ready_ = false;
    std::string                       init_error_;
};
