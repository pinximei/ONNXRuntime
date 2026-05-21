#include "classifier.h"

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
        "Usage: " << argv0 << " [--model PATH] [--top K] [--threads N] [--bench N] IMAGE\n"
        "\n"
        "  --model PATH   Path to the .onnx model. Default: models/efficientnet-lite4-11.onnx\n"
        "  --top   K      Number of predictions to print (default 5)\n"
        "  --threads N    Intra-op threads (default: auto / physical cores)\n"
        "  --bench N      Run inference N times and report average latency\n"
        "  IMAGE          Path to a jpg/png/bmp/gif image\n";
}

int main(int argc, char** argv) {
#ifdef _WIN32
    // Make Chinese console output work.
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::string image_path;
    std::string model_path = "models/efficientnet-lite4-11.onnx";
    int top_k = 5;
    int threads = 0;
    int bench = 0;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value after " << flag << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--model")       model_path = next("--model");
        else if (a == "--top")    top_k      = std::stoi(next("--top"));
        else if (a == "--threads")threads    = std::stoi(next("--threads"));
        else if (a == "--bench")  bench      = std::stoi(next("--bench"));
        else if (a == "-h" || a == "--help") { print_usage(argv[0]); return 0; }
        else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown flag: " << a << "\n";
            print_usage(argv[0]);
            return 2;
        }
        else image_path = a;
    }

    if (image_path.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    // If the model path is relative and not found next to CWD, try next to the exe.
    if (!fs::exists(model_path)) {
        fs::path exe_dir = fs::path(argv[0]).parent_path();
        fs::path alt = exe_dir / model_path;
        if (fs::exists(alt)) model_path = alt.string();
    }
    if (!fs::exists(model_path)) {
        std::cerr << "Model file not found: " << model_path << "\n"
                  << "Run scripts\\fetch_model.ps1 first.\n";
        return 1;
    }
    if (!fs::exists(image_path)) {
        std::cerr << "Image not found: " << image_path << "\n";
        return 1;
    }

    try {
        std::wstring wmodel(model_path.begin(), model_path.end());

        auto t0 = std::chrono::steady_clock::now();
        cls::Classifier clf(wmodel, threads);
        auto t1 = std::chrono::steady_clock::now();
        double load_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        auto preds = clf.classify_file(image_path, top_k);
        auto t2 = std::chrono::steady_clock::now();
        double first_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

        std::cout << "Model:  " << model_path
                  << "  (loaded in " << static_cast<int>(load_ms) << " ms)\n";
        std::cout << "Image:  " << image_path
                  << "  (inference " << static_cast<int>(first_ms) << " ms)\n";
        std::cout << "Top-" << preds.size() << ":\n";
        for (size_t i = 0; i < preds.size(); ++i) {
            std::printf("  %zu. [%4d] %-40s  %6.2f%%\n",
                        i + 1, preds[i].class_id,
                        preds[i].label.c_str(),
                        preds[i].score * 100.0f);
        }

        if (bench > 0) {
            auto b0 = std::chrono::steady_clock::now();
            for (int i = 0; i < bench; ++i) {
                clf.classify_file(image_path, top_k);
            }
            auto b1 = std::chrono::steady_clock::now();
            double total = std::chrono::duration<double, std::milli>(b1 - b0).count();
            std::cout << "Benchmark: " << bench << " runs, avg "
                      << (total / bench) << " ms/run\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
