#include "stdafx.h"
#include "ImageView.h"

BEGIN_MESSAGE_MAP(CImageView, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
END_MESSAGE_MAP()

CImageView::CImageView() = default;
CImageView::~CImageView() {
    if (hbm_) ::DeleteObject(hbm_);
}

BOOL CImageView::Create(DWORD dwStyle, const RECT& rect, CWnd* parent, UINT id) {
    LPCTSTR cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
                                      ::LoadCursor(nullptr, IDC_ARROW),
                                      (HBRUSH)(COLOR_APPWORKSPACE + 1),
                                      nullptr);
    return CWnd::Create(cls, _T("ImageView"), dwStyle, rect, parent, id);
}

void CImageView::ClearAll() {
    rgb_.clear();
    img_w_ = img_h_ = 0;
    boxes_.clear();
    texts_.clear();
    highlight_ = -1;
    if (hbm_) { ::DeleteObject(hbm_); hbm_ = nullptr; }
    bgra_.clear();
    if (GetSafeHwnd()) Invalidate();
}

void CImageView::SetImage(const std::vector<unsigned char>& rgb, int w, int h) {
    rgb_ = rgb;
    img_w_ = w;
    img_h_ = h;
    boxes_.clear();
    texts_.clear();
    highlight_ = -1;
    RebuildDib();
    if (GetSafeHwnd()) Invalidate();
}

void CImageView::SetDetections(const std::vector<uid::Box>& boxes,
                               const std::vector<uid::RecResult>& texts) {
    boxes_ = boxes;
    texts_ = texts;
    highlight_ = -1;
    if (GetSafeHwnd()) Invalidate();
}

void CImageView::SetHighlight(int idx) {
    highlight_ = idx;
    if (GetSafeHwnd()) Invalidate();
}

void CImageView::RebuildDib() {
    if (hbm_) { ::DeleteObject(hbm_); hbm_ = nullptr; }
    if (rgb_.empty()) return;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = img_w_;
    bi.bmiHeader.biHeight      = -img_h_;       // negative = top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    hbm_ = ::CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    if (!hbm_ || !pvBits) return;

    // Convert RGB -> BGRA (Windows DIB).
    auto* dst = static_cast<unsigned char*>(pvBits);
    const auto* src = rgb_.data();
    for (int i = 0, n = img_w_ * img_h_; i < n; ++i) {
        dst[i * 4 + 0] = src[i * 3 + 2]; // B
        dst[i * 4 + 1] = src[i * 3 + 1]; // G
        dst[i * 4 + 2] = src[i * 3 + 0]; // R
        dst[i * 4 + 3] = 255;
    }
}

void CImageView::ComputeFit(int& dx, int& dy, double& scale) const {
    CRect rc; GetClientRect(&rc);
    if (img_w_ <= 0 || img_h_ <= 0) { dx = dy = 0; scale = 1.0; return; }
    double sx = static_cast<double>(rc.Width())  / img_w_;
    double sy = static_cast<double>(rc.Height()) / img_h_;
    scale = (sx < sy) ? sx : sy;
    if (scale > 1.0) scale = 1.0;  // never upscale
    int draw_w = static_cast<int>(img_w_ * scale);
    int draw_h = static_cast<int>(img_h_ * scale);
    dx = (rc.Width()  - draw_w) / 2;
    dy = (rc.Height() - draw_h) / 2;
}

BOOL CImageView::OnEraseBkgnd(CDC* /*pDC*/) { return TRUE; }

void CImageView::OnSize(UINT nType, int cx, int cy) {
    CWnd::OnSize(nType, cx, cy);
    Invalidate();
}

void CImageView::OnPaint() {
    CPaintDC dc(this);
    CRect rc; GetClientRect(&rc);

    // Double-buffer to avoid flicker.
    CDC mem; mem.CreateCompatibleDC(&dc);
    CBitmap bmp; bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* old_bmp = mem.SelectObject(&bmp);

    mem.FillSolidRect(&rc, RGB(40, 40, 44));

    if (!hbm_) {
        CFont f;
        f.CreatePointFont(110, _T("Segoe UI"));
        CFont* old_f = mem.SelectObject(&f);
        mem.SetTextColor(RGB(200, 200, 200));
        mem.SetBkMode(TRANSPARENT);
        mem.DrawText(_T("Click 'Open image' to load a screenshot."),
                     &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        mem.SelectObject(old_f);
    } else {
        int dx, dy;
        double scale;
        ComputeFit(dx, dy, scale);
        int draw_w = static_cast<int>(img_w_ * scale);
        int draw_h = static_cast<int>(img_h_ * scale);

        CDC src; src.CreateCompatibleDC(&dc);
        HGDIOBJ old_src = src.SelectObject(hbm_);
        mem.SetStretchBltMode(HALFTONE);
        ::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, nullptr);
        mem.StretchBlt(dx, dy, draw_w, draw_h, &src, 0, 0, img_w_, img_h_, SRCCOPY);
        src.SelectObject(old_src);

        // Draw boxes.
        static const COLORREF palette[] = {
            RGB(255, 80, 80), RGB(80, 220, 80), RGB(80, 160, 255),
            RGB(255, 200, 0), RGB(255, 80, 255), RGB(0, 220, 220),
            RGB(255, 140, 0), RGB(180, 100, 255)
        };
        for (size_t i = 0; i < boxes_.size(); ++i) {
            const auto& b = boxes_[i];
            int x0 = dx + static_cast<int>(b.x * scale);
            int y0 = dy + static_cast<int>(b.y * scale);
            int x1 = dx + static_cast<int>((b.x + b.w) * scale);
            int y1 = dy + static_cast<int>((b.y + b.h) * scale);
            bool hot = (static_cast<int>(i) == highlight_);
            COLORREF col = hot ? RGB(255, 255, 0) : palette[i % (sizeof(palette)/sizeof(palette[0]))];
            int thick = hot ? 3 : 1;
            CPen pen(PS_SOLID, thick, col);
            CPen* old_pen = mem.SelectObject(&pen);
            HGDIOBJ old_brush = ::SelectObject(mem.GetSafeHdc(), ::GetStockObject(NULL_BRUSH));
            mem.Rectangle(x0, y0, x1, y1);
            ::SelectObject(mem.GetSafeHdc(), old_brush);
            mem.SelectObject(old_pen);
        }
    }

    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(old_bmp);
}
