#include "image_io.h"
#include "recognizer.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

namespace uid {

uint8_t* load_image_rgb(const std::string& path, int* w, int* h) {
    int ch = 0;
    return stbi_load(path.c_str(), w, h, &ch, 3);
}

void free_image(uint8_t* p) { if (p) stbi_image_free(p); }

namespace {

inline void put_pixel(uint8_t* buf, int W, int H, int x, int y,
                      uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    uint8_t* p = buf + (y * W + x) * 3;
    p[0] = r; p[1] = g; p[2] = b;
}

// Draw a hollow rectangle with a given thickness.
void draw_rect(uint8_t* buf, int W, int H,
               int x0, int y0, int x1, int y1, int thickness,
               uint8_t r, uint8_t g, uint8_t b)
{
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    for (int t = 0; t < thickness; ++t) {
        for (int x = x0; x <= x1; ++x) {
            put_pixel(buf, W, H, x, y0 + t, r, g, b);
            put_pixel(buf, W, H, x, y1 - t, r, g, b);
        }
        for (int y = y0; y <= y1; ++y) {
            put_pixel(buf, W, H, x0 + t, y, r, g, b);
            put_pixel(buf, W, H, x1 - t, y, r, g, b);
        }
    }
}

} // namespace

bool save_annotated_png(const std::string& out_path,
                        const uint8_t* rgb, int w, int h,
                        const std::vector<Box>& boxes)
{
    std::vector<uint8_t> canvas(rgb, rgb + static_cast<size_t>(w) * h * 3);

    // Cycle through a small palette so adjacent boxes are distinguishable.
    static const uint8_t palette[][3] = {
        {255,  80,  80}, { 80, 255,  80}, { 80, 160, 255}, {255, 200,   0},
        {255,  80, 255}, {  0, 220, 220}, {255, 140,   0}, {180, 100, 255},
    };
    const int npal = sizeof(palette) / sizeof(palette[0]);

    for (size_t i = 0; i < boxes.size(); ++i) {
        const Box& b = boxes[i];
        int x0 = static_cast<int>(b.x);
        int y0 = static_cast<int>(b.y);
        int x1 = static_cast<int>(b.x + b.w);
        int y1 = static_cast<int>(b.y + b.h);
        const uint8_t* col = palette[i % npal];
        draw_rect(canvas.data(), w, h, x0, y0, x1, y1, 2, col[0], col[1], col[2]);
    }
    return stbi_write_png(out_path.c_str(), w, h, 3, canvas.data(), w * 3) != 0;
}

namespace {
// Minimal JSON string escaper. Input is expected to be UTF-8.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else out += static_cast<char>(c);
        }
    }
    return out;
}
} // namespace

bool save_json(const std::string& out_path,
               int img_w, int img_h,
               const std::vector<Box>& boxes,
               const std::vector<RecResult>& texts)
{
    std::ofstream f(out_path);
    if (!f) return false;
    const bool has_text = !texts.empty() && texts.size() == boxes.size();
    f << "{\n";
    f << "  \"image\": {\"w\": " << img_w << ", \"h\": " << img_h << "},\n";
    f << "  \"count\": " << boxes.size() << ",\n";
    f << "  \"boxes\": [\n";
    for (size_t i = 0; i < boxes.size(); ++i) {
        const Box& b = boxes[i];
        f << "    {\"id\": " << i
          << ", \"x\": " << static_cast<int>(b.x)
          << ", \"y\": " << static_cast<int>(b.y)
          << ", \"w\": " << static_cast<int>(b.w)
          << ", \"h\": " << static_cast<int>(b.h)
          << ", \"score\": " << b.score;
        if (has_text) {
            f << ", \"text\": \"" << json_escape(texts[i].text) << "\""
              << ", \"text_score\": " << texts[i].score;
        }
        f << "}";
        if (i + 1 < boxes.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n";
    f << "}\n";
    return true;
}

} // namespace uid
