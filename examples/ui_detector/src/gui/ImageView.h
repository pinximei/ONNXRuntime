#pragma once
#include "stdafx.h"
#include "detector.h"
#include "recognizer.h"

// A scrollable image view that paints an RGB bitmap and overlays detection boxes.
class CImageView : public CWnd {
public:
    CImageView();
    virtual ~CImageView();

    BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* parent, UINT id);

    // Takes ownership of a copy of the pixels.
    void SetImage(const std::vector<unsigned char>& rgb, int w, int h);
    void SetDetections(const std::vector<uid::Box>& boxes,
                       const std::vector<uid::RecResult>& texts);
    void SetHighlight(int box_index);  // -1 = none
    void ClearAll();

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    DECLARE_MESSAGE_MAP()

private:
    void RebuildDib();
    void ComputeFit(int& dx, int& dy, double& scale) const;

    std::vector<unsigned char> rgb_;        // original image, top-down RGB
    int img_w_ = 0, img_h_ = 0;

    HBITMAP hbm_ = nullptr;                  // cached BGRA DIB matching rgb_
    std::vector<unsigned char> bgra_;        // for the DIB section

    std::vector<uid::Box> boxes_;
    std::vector<uid::RecResult> texts_;
    int highlight_ = -1;
};
