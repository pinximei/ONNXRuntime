#include "stdafx.h"
#include "MainFrame.h"
#include "Resource.h"
#include "image_io.h"

#include <fstream>

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_BN_CLICKED(IDC_BTN_OPEN,   &CMainFrame::OnBtnOpen)
    ON_BN_CLICKED(IDC_BTN_RERUN,  &CMainFrame::OnBtnRerun)
    ON_BN_CLICKED(IDC_BTN_EXPORT, &CMainFrame::OnBtnExport)
    ON_BN_CLICKED(IDC_CHK_SERVER, &CMainFrame::OnChkServer)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST, &CMainFrame::OnListItemChanged)
    ON_MESSAGE(WM_INFERENCE_DONE, &CMainFrame::OnInferenceDone)
END_MESSAGE_MAP()

CMainFrame::CMainFrame() = default;
CMainFrame::~CMainFrame() = default;

namespace {
// Resolve a model path: first relative to CWD, then exe directory.
CString FindAsset(LPCTSTR rel) {
    if (::PathFileExists(rel)) return CString(rel);
    TCHAR exe[MAX_PATH] = {};
    ::GetModuleFileName(nullptr, exe, MAX_PATH);
    CString p = exe;
    int slash = p.ReverseFind(_T('\\'));
    if (slash > 0) p.Truncate(slash + 1);
    p += rel;
    return p;
}

std::wstring towstr(LPCTSTR s) { return std::wstring(s); }

CString Utf8ToCString(const std::string& s) {
    if (s.empty()) return CString();
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    CString out;
    LPWSTR buf = out.GetBuffer(n);
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), buf, n);
    out.ReleaseBuffer(n);
    return out;
}
} // namespace

int CMainFrame::OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;

    ui_font_.CreatePointFont(95, _T("Segoe UI"));

    // Image view (left).
    image_view_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER,
                       CRect(0, 0, 100, 100), this, IDC_IMAGE_VIEW);

    // List (right).
    list_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER |
                 LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                 CRect(0, 0, 100, 100), this, IDC_LIST);
    list_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    list_.SetFont(&ui_font_);
    list_.InsertColumn(0, _T("#"),     LVCFMT_RIGHT, 40);
    list_.InsertColumn(1, _T("X"),     LVCFMT_RIGHT, 55);
    list_.InsertColumn(2, _T("Y"),     LVCFMT_RIGHT, 55);
    list_.InsertColumn(3, _T("W"),     LVCFMT_RIGHT, 55);
    list_.InsertColumn(4, _T("H"),     LVCFMT_RIGHT, 55);
    list_.InsertColumn(5, _T("Det"),   LVCFMT_RIGHT, 55);
    list_.InsertColumn(6, _T("OCR"),   LVCFMT_RIGHT, 55);
    list_.InsertColumn(7, _T("Text"),  LVCFMT_LEFT, 380);

    // Buttons (bottom).
    btn_open_.Create(_T("Open image..."), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     CRect(0, 0, 100, 30), this, IDC_BTN_OPEN);
    btn_rerun_.Create(_T("Re-run"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      CRect(0, 0, 100, 30), this, IDC_BTN_RERUN);
    btn_export_.Create(_T("Export JSON..."), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                       CRect(0, 0, 100, 30), this, IDC_BTN_EXPORT);
    status_.Create(_T("Loading models..."),
                   WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                   CRect(0, 0, 100, 24), this, IDC_STATUS);
    btn_open_.SetFont(&ui_font_);
    btn_rerun_.SetFont(&ui_font_);
    btn_export_.SetFont(&ui_font_);
    status_.SetFont(&ui_font_);
    btn_rerun_.EnableWindow(FALSE);
    btn_export_.EnableWindow(FALSE);

    // Server-mode checkbox (only enabled if the file exists on disk).
    chk_server_.Create(_T("Server OCR (better Chinese, BUT ~100x slower — 60-90 s per image)"),
                       WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                       CRect(0, 0, 100, 24), this, IDC_CHK_SERVER);
    chk_server_.SetFont(&ui_font_);

    // Resolve all asset paths once; we'll swap cn model when the checkbox toggles.
    CString det_path     = FindAsset(_T("models\\omniparser-icon_detect-640.onnx"));
    CString mobile_path  = FindAsset(_T("models\\ppocrv4_rec.onnx"));
    CString server_path  = FindAsset(_T("models\\ppocrv4_rec_server.onnx"));
    CString cn_dict_path = FindAsset(_T("models\\ppocr_keys_v1.txt"));
    CString en_rec_path  = FindAsset(_T("models\\en_ppocrv3_rec.onnx"));
    CString en_dict_path = FindAsset(_T("models\\en_dict.txt"));

    detect_path_     = towstr(det_path);
    mobile_cn_path_  = towstr(mobile_path);
    server_cn_path_  = towstr(server_path);
    en_path_         = towstr(en_rec_path);
    CT2A cn_dict_a(cn_dict_path, CP_UTF8);
    CT2A en_dict_a(en_dict_path, CP_UTF8);
    cn_dict_ = std::string((LPCSTR)cn_dict_a);
    en_dict_ = std::string((LPCSTR)en_dict_a);

    // Disable server checkbox if the server ONNX isn't downloaded.
    if (!::PathFileExists(server_path)) {
        chk_server_.EnableWindow(FALSE);
        chk_server_.SetWindowText(_T("Server OCR model not downloaded (run fetch_ocr.ps1 -Server)"));
    }

    if (!RebuildWorker(false)) {
        btn_open_.EnableWindow(FALSE);
    } else {
        SetStatus(_T("Ready. Click 'Open image' to begin."));
    }
    return 0;
}

bool CMainFrame::RebuildWorker(bool use_server) {
    Worker::AssetPaths paths;
    paths.detect_model = detect_path_;
    paths.cn_rec_model = use_server ? server_cn_path_ : mobile_cn_path_;
    paths.cn_dict      = cn_dict_;
    paths.en_rec_model = en_path_;
    paths.en_dict      = en_dict_;
    worker_ = std::make_unique<Worker>(this, paths);
    if (!worker_->ready()) {
        SetStatus(CString(_T("Init failed: ")) + Utf8ToCString(worker_->init_error()));
        return false;
    }
    return true;
}

void CMainFrame::OnSize(UINT nType, int cx, int cy) {
    CFrameWnd::OnSize(nType, cx, cy);
    Layout();
}

void CMainFrame::Layout() {
    CRect rc; GetClientRect(&rc);
    const int margin   = 6;
    const int btn_row  = 36;
    const int sep_x    = static_cast<int>(rc.Width() * 0.55);

    int top    = margin;
    int bottom = rc.Height() - btn_row - margin;

    if (image_view_.GetSafeHwnd()) {
        image_view_.MoveWindow(margin, top, sep_x - margin - 4, bottom - top);
    }
    if (list_.GetSafeHwnd()) {
        list_.MoveWindow(sep_x + 4, top, rc.Width() - sep_x - 4 - margin, bottom - top);
    }
    int by = rc.Height() - btn_row - 2;
    int bx = margin;
    int bw = 130, bh = 30;
    if (btn_open_.GetSafeHwnd())   { btn_open_.MoveWindow(bx, by, bw, bh);   bx += bw + 8; }
    if (btn_rerun_.GetSafeHwnd())  { btn_rerun_.MoveWindow(bx, by, bw, bh);  bx += bw + 8; }
    if (btn_export_.GetSafeHwnd()) { btn_export_.MoveWindow(bx, by, bw, bh); bx += bw + 16; }
    if (chk_server_.GetSafeHwnd()) { chk_server_.MoveWindow(bx, by, 450, bh); bx += 450 + 12; }
    if (status_.GetSafeHwnd())     { status_.MoveWindow(bx, by, rc.Width() - bx - margin, bh); }
}

void CMainFrame::OnChkServer() {
    if (busy_) {
        // Don't allow toggling during inference; revert visual state.
        chk_server_.SetCheck(chk_server_.GetCheck() ? BST_UNCHECKED : BST_CHECKED);
        return;
    }
    bool use_server = (chk_server_.GetCheck() == BST_CHECKED);
    SetStatus(use_server ? _T("Loading server OCR model...")
                         : _T("Loading mobile OCR model..."));
    btn_open_.EnableWindow(FALSE);
    btn_rerun_.EnableWindow(FALSE);
    btn_export_.EnableWindow(FALSE);
    chk_server_.EnableWindow(FALSE);

    bool ok = RebuildWorker(use_server);

    chk_server_.EnableWindow(::PathFileExists(server_cn_path_.c_str()));
    btn_open_.EnableWindow(ok);
    btn_rerun_.EnableWindow(ok && !last_image_path_.empty());
    btn_export_.EnableWindow(ok && current_ && !current_->boxes.empty());
    if (ok) {
        SetStatus(use_server ? _T("Switched to server model. Re-run image to apply.")
                             : _T("Switched to mobile model. Re-run image to apply."));
    }
}

void CMainFrame::SetStatus(const CString& s) {
    if (status_.GetSafeHwnd()) status_.SetWindowText(s);
}

void CMainFrame::OnBtnOpen() {
    if (busy_) return;
    CFileDialog dlg(TRUE, nullptr, nullptr,
                    OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    _T("Images (*.png;*.jpg;*.jpeg;*.bmp;*.gif)|*.png;*.jpg;*.jpeg;*.bmp;*.gif|All files (*.*)|*.*||"),
                    this);
    if (dlg.DoModal() != IDOK) return;
    last_image_path_ = dlg.GetPathName().GetString();
    RunInference(last_image_path_);
}

void CMainFrame::OnBtnRerun() {
    if (busy_ || last_image_path_.empty()) return;
    RunInference(last_image_path_);
}

void CMainFrame::OnBtnExport() {
    if (!current_ || current_->boxes.empty()) return;
    CFileDialog dlg(FALSE, _T("json"), _T("boxes.json"),
                    OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                    _T("JSON (*.json)|*.json||"), this);
    if (dlg.DoModal() != IDOK) return;
    CT2A path_a(dlg.GetPathName(), CP_UTF8);
    bool ok = uid::save_json(std::string((LPCSTR)path_a),
                             current_->w, current_->h,
                             current_->boxes, current_->texts);
    SetStatus(ok ? _T("JSON exported.") : _T("Export failed."));
}

void CMainFrame::RunInference(const std::wstring& path) {
    busy_ = true;
    btn_open_.EnableWindow(FALSE);
    btn_rerun_.EnableWindow(FALSE);
    btn_export_.EnableWindow(FALSE);
    SetStatus(_T("Running detection + OCR..."));
    list_.DeleteAllItems();
    worker_->Run(path);
}

LRESULT CMainFrame::OnInferenceDone(WPARAM, LPARAM lp) {
    std::unique_ptr<InferenceResult> r(reinterpret_cast<InferenceResult*>(lp));
    busy_ = false;
    btn_open_.EnableWindow(TRUE);
    btn_rerun_.EnableWindow(TRUE);

    if (!r->error.empty()) {
        SetStatus(CString(_T("Error: ")) + Utf8ToCString(r->error));
        btn_export_.EnableWindow(FALSE);
        return 0;
    }

    image_view_.SetImage(r->rgb, r->w, r->h);
    image_view_.SetDetections(r->boxes, r->texts);
    current_ = std::move(r);
    PopulateList();
    btn_export_.EnableWindow(current_->boxes.empty() ? FALSE : TRUE);

    CString msg;
    msg.Format(_T("Done. %d boxes (det %d ms, OCR %d ms)."),
               (int)current_->boxes.size(),
               (int)current_->detect_ms,
               (int)current_->ocr_ms);
    SetStatus(msg);
    return 0;
}

void CMainFrame::PopulateList() {
    list_.DeleteAllItems();
    if (!current_) return;
    CString tmp;
    for (size_t i = 0; i < current_->boxes.size(); ++i) {
        const auto& b = current_->boxes[i];
        tmp.Format(_T("%zu"), i); list_.InsertItem((int)i, tmp);
        tmp.Format(_T("%d"), (int)b.x);          list_.SetItemText((int)i, 1, tmp);
        tmp.Format(_T("%d"), (int)b.y);          list_.SetItemText((int)i, 2, tmp);
        tmp.Format(_T("%d"), (int)b.w);          list_.SetItemText((int)i, 3, tmp);
        tmp.Format(_T("%d"), (int)b.h);          list_.SetItemText((int)i, 4, tmp);
        tmp.Format(_T("%.2f"), b.score);          list_.SetItemText((int)i, 5, tmp);
        if (i < current_->texts.size()) {
            tmp.Format(_T("%.2f"), current_->texts[i].score);
            list_.SetItemText((int)i, 6, tmp);
            list_.SetItemText((int)i, 7, Utf8ToCString(current_->texts[i].text));
        }
    }
}

void CMainFrame::OnListItemChanged(NMHDR* pNMHDR, LRESULT* pResult) {
    auto* lv = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
    if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED)) {
        image_view_.SetHighlight(lv->iItem);
    }
    *pResult = 0;
}
