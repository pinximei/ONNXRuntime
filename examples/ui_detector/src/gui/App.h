#pragma once
#include "stdafx.h"

class CDetectApp : public CWinApp {
public:
    CDetectApp();
    BOOL InitInstance() override;
    int  ExitInstance() override;

    DECLARE_MESSAGE_MAP()
};

extern CDetectApp theApp;
