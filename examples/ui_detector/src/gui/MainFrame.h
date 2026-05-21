#pragma once
#include "stdafx.h"
#include "ImageView.h"
#include "Worker.h"

class CMainFrame : public CFrameWnd {
public:
    CMainFrame();
    virtual ~CMainFrame();

protected:
    afx_msg int  OnCreate(LPCREATESTRUCT lpcs);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnBtnOpen();
    afx_msg void OnBtnRerun();
    afx_msg void OnBtnExport();
    afx_msg void OnChkServer();
    afx_msg void OnListItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg LRESULT OnInferenceDone(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    void Layout();
    void SetStatus(const CString& s);
    void RunInference(const std::wstring& path);
    void PopulateList();
    bool RebuildWorker(bool use_server);

    CImageView image_view_;
    CListCtrl  list_;
    CButton    btn_open_;
    CButton    btn_rerun_;
    CButton    btn_export_;
    CButton    chk_server_;
    CStatic    status_;
    CFont      ui_font_;

    std::unique_ptr<Worker> worker_;
    std::unique_ptr<InferenceResult> current_;
    std::wstring last_image_path_;
    std::wstring mobile_cn_path_;
    std::wstring server_cn_path_;
    std::wstring detect_path_;
    std::wstring en_path_;
    std::string  cn_dict_;
    std::string  en_dict_;
    bool busy_ = false;
};
