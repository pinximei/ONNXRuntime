#include "detector.h"
#include "image_io.h"
#include "ocr_pipeline.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace fs = std::filesystem;

static void print_usage(const char* argv0) {
    std::cout <<
        "Usage: " << argv0 << " [options] IMAGE\n"
        "\n"
        "  --model PATH     Detector ONNX. Default: models/omniparser-icon_detect-640.onnx\n"
        "  --rec PATH       Chinese rec ONNX. Default: models/ppocrv4_rec.onnx\n"
        "  --dict PATH      Chinese char dict. Default: models/ppocr_keys_v1.txt\n"
        "  --rec-en PATH    English rec ONNX. Default: models/en_ppocrv3_rec.onnx\n"
        "  --dict-en PATH   English char dict. Default: models/en_dict.txt\n"
        "  --no-ocr         Skip OCR (detection only)\n"
        "  --conf F         Detection confidence threshold (default 0.05)\n"
        "  --iou  F         Detection NMS IoU threshold (default 0.50)\n"
        "  --min-text-score F  Drop OCR results below this score (default 0.50)\n"
        "  --threads N      Intra-op threads (0=auto)\n"
        "  --out-png PATH   Annotated image (default: <IMAGE>_annotated.png)\n"
        "  --out-json PATH  Detections JSON (default: <IMAGE>_boxes.json)\n";
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::string image_path;
    std::string model_path  = "models/omniparser-icon_detect-640.onnx";
    std::string rec_path    = "models/ppocrv4_rec.onnx";
    std::string dict_path   = "models/ppocr_keys_v1.txt";
    std::string rec_en_path = "models/en_ppocrv3_rec.onnx";
    std::string dict_en_path= "models/en_dict.txt";
    std::string out_png, out_json;
    bool        do_ocr = true;
    float       min_text_score = 0.50f;
    uid::DetectorOptions opt;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* f) {
            if (i + 1 >= argc) { std::cerr << "Missing value after " << f << "\n"; std::exit(2); }
            return std::string(argv[++i]);
        };
        if      (a == "--model")    model_path    = next("--model");
        else if (a == "--rec")      rec_path      = next("--rec");
        else if (a == "--dict")     dict_path     = next("--dict");
        else if (a == "--rec-en")   rec_en_path   = next("--rec-en");
        else if (a == "--dict-en")  dict_en_path  = next("--dict-en");
        else if (a == "--no-ocr")   do_ocr        = false;
        else if (a == "--conf")     opt.conf_threshold = std::stof(next("--conf"));
        else if (a == "--iou")      opt.iou_threshold  = std::stof(next("--iou"));
        else if (a == "--min-text-score") min_text_score = std::stof(next("--min-text-score"));
        else if (a == "--threads")  opt.intra_threads  = std::stoi(next("--threads"));
        else if (a == "--out-png")  out_png  = next("--out-png");
        else if (a == "--out-json") out_json = next("--out-json");
        else if (a == "-h" || a == "--help") { print_usage(argv[0]); return 0; }
        else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown flag: " << a << "\n";
            print_usage(argv[0]); return 2;
        }
        else image_path = a;
    }

    if (image_path.empty()) { print_usage(argv[0]); return 2; }

    if (!fs::exists(model_path)) {
        fs::path alt = fs::path(argv[0]).parent_path() / model_path;
        if (fs::exists(alt)) model_path = alt.string();
    }
    if (!fs::exists(model_path)) {
        std::cerr << "Model not found: " << model_path
                  << "\nRun scripts\\fetch_model.ps1 first.\n";
        return 1;
    }
    if (!fs::exists(image_path)) {
        std::cerr << "Image not found: " << image_path << "\n";
        return 1;
    }

    fs::path img_p(image_path);
    if (out_png.empty())  out_png  = (img_p.parent_path() / (img_p.stem().string() + "_annotated.png")).string();
    if (out_json.empty()) out_json = (img_p.parent_path() / (img_p.stem().string() + "_boxes.json")).string();

    try {
        std::wstring wmodel(model_path.begin(), model_path.end());

        auto ms = [](auto a, auto b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };

        auto t0 = std::chrono::steady_clock::now();
        uid::Detector det(wmodel, opt);
        auto t1 = std::chrono::steady_clock::now();

        std::unique_ptr<uid::OcrPipeline> ocr;
        if (do_ocr) {
            for (const auto& p : { rec_path, dict_path, rec_en_path, dict_en_path }) {
                if (!fs::exists(p)) {
                    std::cerr << "OCR asset missing: " << p
                              << ". Run scripts\\fetch_ocr.ps1, or pass --no-ocr.\n";
                    return 1;
                }
            }
            uid::OcrPipeline::Paths pp;
            pp.cn_model = std::wstring(rec_path.begin(),    rec_path.end());
            pp.cn_dict  = dict_path;
            pp.en_model = std::wstring(rec_en_path.begin(), rec_en_path.end());
            pp.en_dict  = dict_en_path;
            ocr = std::make_unique<uid::OcrPipeline>(pp, opt.intra_threads);
        }
        auto t2 = std::chrono::steady_clock::now();

        int W = 0, H = 0;
        uint8_t* img = uid::load_image_rgb(image_path, &W, &H);
        if (!img) { std::cerr << "Failed to decode image\n"; return 1; }

        auto t3 = std::chrono::steady_clock::now();
        auto boxes = det.detect(img, W, H);
        auto t4 = std::chrono::steady_clock::now();

        std::vector<uid::RecResult> texts;
        if (do_ocr) {
            texts = ocr->recognize_boxes(img, W, H, boxes);
            for (auto& r : texts) {
                if (r.score < min_text_score) { r.text.clear(); r.score = 0.f; }
            }
        }
        auto t5 = std::chrono::steady_clock::now();

        uid::save_annotated_png(out_png, img, W, H, boxes);
        uid::save_json(out_json, W, H, boxes, texts);
        uid::free_image(img);

        std::cout << "Detector:  " << model_path << "  (load " << static_cast<int>(ms(t0,t1)) << " ms)\n";
        if (do_ocr) std::cout << "OCR:       cn=" << rec_path << ", en=" << rec_en_path
                              << "  (load " << static_cast<int>(ms(t1,t2)) << " ms)\n";
        std::cout << "Image:     " << image_path << "  (" << W << "x" << H << ")\n";
        std::cout << "Detected:  " << boxes.size() << " boxes  (infer "
                  << static_cast<int>(ms(t3,t4)) << " ms)\n";
        if (do_ocr) std::cout << "OCR:       " << texts.size() << " crops  ("
                              << static_cast<int>(ms(t4,t5)) << " ms total, avg "
                              << (boxes.empty() ? 0 : static_cast<int>(ms(t4,t5) / boxes.size())) << " ms/box)\n";
        std::cout << "Output:    " << out_png << "\n";
        std::cout << "           " << out_json << "\n\n";

        // Print every box that has text, plus top 5 textless boxes.
        std::vector<size_t> with_text, without_text;
        if (do_ocr) {
            for (size_t i = 0; i < boxes.size(); ++i) {
                if (!texts[i].text.empty()) with_text.push_back(i);
                else                         without_text.push_back(i);
            }
        }
        if (do_ocr && !with_text.empty()) {
            std::cout << "Boxes with text (" << with_text.size() << "):\n";
            for (size_t k = 0; k < with_text.size(); ++k) {
                size_t i = with_text[k];
                const auto& b = boxes[i];
                std::printf("  %3zu. x=%4d y=%4d w=%4d h=%4d  det=%.2f  ocr=%.2f  | %s\n",
                            k + 1,
                            static_cast<int>(b.x), static_cast<int>(b.y),
                            static_cast<int>(b.w), static_cast<int>(b.h),
                            b.score, texts[i].score, texts[i].text.c_str());
            }
        }
        const size_t n_print = std::min<size_t>(do_ocr ? without_text.size() : boxes.size(), 10);
        if (n_print > 0) {
            std::cout << (do_ocr ? "\nBoxes without text (top " : "Top ")
                      << n_print << "):\n";
        }
        for (size_t k = 0; k < n_print; ++k) {
            size_t i = do_ocr ? without_text[k] : k;
            const auto& b = boxes[i];
            std::printf("  %2zu. x=%4d y=%4d w=%4d h=%4d  score=%.3f\n",
                        k + 1,
                        static_cast<int>(b.x), static_cast<int>(b.y),
                        static_cast<int>(b.w), static_cast<int>(b.h),
                        b.score);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
