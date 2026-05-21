#include "ocr_pipeline.h"

#include <algorithm>
#include <cctype>
#include <functional>

namespace uid {

namespace {

// Iterate UTF-8 code points, returning each as a uint32_t.
// On invalid sequence, returns false.
bool foreach_codepoint(const std::string& s, const std::function<void(uint32_t)>& cb) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int n = 0;
        if      ((c & 0x80) == 0x00) { cp = c;        n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
        else return false;
        if (i + n > s.size()) return false;
        for (int k = 1; k < n; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        cb(cp);
        i += n;
    }
    return true;
}

struct CharStats {
    int total       = 0;
    int ascii_alnum = 0;   // [0-9A-Za-z]
    int ascii_other = 0;   // ASCII but not alnum (space, punct)
    int cjk         = 0;   // CJK ideographs
    int other       = 0;
};

CharStats classify_text(const std::string& s) {
    CharStats st;
    foreach_codepoint(s, [&](uint32_t cp) {
        ++st.total;
        if (cp < 0x80) {
            if ((cp >= '0' && cp <= '9') ||
                (cp >= 'A' && cp <= 'Z') ||
                (cp >= 'a' && cp <= 'z')) ++st.ascii_alnum;
            else ++st.ascii_other;
        }
        else if ((cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK Unified Ideographs
                 (cp >= 0x3400 && cp <= 0x4DBF) ||   // CJK Ext A
                 (cp >= 0xF900 && cp <= 0xFAFF)) {   // CJK Compatibility
            ++st.cjk;
        }
        else ++st.other;
    });
    return st;
}

// Hard-drop criteria for OCR results that look like noise.
bool looks_like_noise(const std::string& text, const CharStats& st) {
    if (st.total == 0) return true;
    // Single non-alphanumeric character: usually a misread icon.
    if (st.total == 1 && st.ascii_alnum == 0 && st.cjk == 0) return true;
    // "Other" category dominates (random unicode symbols) -> probably an icon.
    if (st.other > 0 && st.other * 2 >= st.total) return true;
    (void)text;
    return false;
}

// Decide between the Chinese-model result and the English-model result.
RecResult pick_best(const RecResult& cn, const RecResult& en) {
    CharStats st_cn = classify_text(cn.text);
    CharStats st_en = classify_text(en.text);

    bool cn_has_cjk    = st_cn.cjk > 0;
    bool en_is_ascii   = st_en.total > 0 && (st_en.ascii_alnum + st_en.ascii_other) == st_en.total;

    // If the English-only model produced a confident, all-ASCII reading, prefer it.
    // (The Chinese model has no Latin alphabet beyond a few common letters, so it
    // tends to misread runs of English letters as Chinese characters.)
    if (en_is_ascii && st_en.ascii_alnum > 0 && en.score >= 0.75f) {
        // Unless the Chinese model is *also* very confident AND its result contains
        // CJK (i.e. the text really is Chinese with some Latin mixed in).
        if (cn_has_cjk && cn.score >= 0.90f) return cn;
        return en;
    }

    // If the Chinese model produced CJK with decent confidence, prefer it.
    if (cn_has_cjk && cn.score >= 0.60f) return cn;

    // Otherwise pick whichever has the higher score.
    return (cn.score >= en.score) ? cn : en;
}

} // namespace

OcrPipeline::OcrPipeline(const Paths& p, int intra_threads) {
    cn_ = std::make_unique<Recognizer>(p.cn_model, p.cn_dict, intra_threads);
    en_ = std::make_unique<Recognizer>(p.en_model, p.en_dict, intra_threads);
}

std::vector<RecResult> OcrPipeline::recognize_boxes(const uint8_t* img_rgb,
                                                    int img_w, int img_h,
                                                    const std::vector<Box>& boxes,
                                                    int pad_px)
{
    std::vector<RecResult> out;
    out.reserve(boxes.size());

    for (const auto& b : boxes) {
        // --- Geometric pre-filters ---
        // Drop boxes that are too short to contain readable text.
        if (b.h < 12) { out.push_back({ "", 0.f }); continue; }
        // Drop extreme aspect ratios (separator lines, progress bars).
        float aspect = b.w / std::max(1.f, b.h);
        if (aspect > 30 || aspect < 0.1f) { out.push_back({ "", 0.f }); continue; }

        // --- Crop with small padding ---
        int x0 = std::max(0, static_cast<int>(b.x) - pad_px);
        int y0 = std::max(0, static_cast<int>(b.y) - pad_px);
        int x1 = std::min(img_w, static_cast<int>(b.x + b.w) + pad_px);
        int y1 = std::min(img_h, static_cast<int>(b.y + b.h) + pad_px);
        int cw = x1 - x0;
        int ch = y1 - y0;
        if (cw < 4 || ch < 4) { out.push_back({ "", 0.f }); continue; }

        std::vector<uint8_t> crop(static_cast<size_t>(cw) * ch * 3);
        for (int y = 0; y < ch; ++y) {
            const uint8_t* src = img_rgb + ((y0 + y) * img_w + x0) * 3;
            std::copy_n(src, static_cast<size_t>(cw) * 3,
                        crop.data() + static_cast<size_t>(y) * cw * 3);
        }

        // --- Run both recognizers ---
        RecResult r_cn = cn_->recognize(crop.data(), cw, ch);
        RecResult r_en = en_->recognize(crop.data(), cw, ch);

        // --- Choose the better one ---
        RecResult best = pick_best(r_cn, r_en);

        // --- Final noise filter ---
        CharStats st = classify_text(best.text);
        if (looks_like_noise(best.text, st)) { best = { "", 0.f }; }

        out.push_back(std::move(best));
    }
    return out;
}

} // namespace uid
