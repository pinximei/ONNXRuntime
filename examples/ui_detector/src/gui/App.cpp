#include "stdafx.h"
#include "App.h"
#include "MainFrame.h"

CDetectApp theApp;

BEGIN_MESSAGE_MAP(CDetectApp, CWinApp)
END_MESSAGE_MAP()

CDetectApp::CDetectApp() {}

BOOL CDetectApp::InitInstance() {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    CWinApp::InitInstance();

    auto* frame = new CMainFrame();
    if (!frame->Create(nullptr, _T("UI Detector — Screenshot Element Recognizer"),
                       WS_OVERLAPPEDWINDOW, CRect(0, 0, 1400, 820))) {
        return FALSE;
    }
    m_pMainWnd = frame;
    frame->ShowWindow(SW_SHOW);
    frame->UpdateWindow();
    return TRUE;
}

int CDetectApp::ExitInstance() {
    return CWinApp::ExitInstance();
}
