#pragma once

#define IDD_MAIN              101
#define IDC_IMAGE_VIEW       1001
#define IDC_LIST             1002
#define IDC_BTN_OPEN         1003
#define IDC_BTN_RERUN        1004
#define IDC_BTN_EXPORT       1005
#define IDC_STATUS           1006
#define IDC_CHK_SERVER       1007

// Custom message: worker thread -> UI when inference finishes.
#define WM_INFERENCE_DONE    (WM_APP + 1)
