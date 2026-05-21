#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "detector.h"

namespace uid {

// Load image as RGB uint8 (3 channels). Caller must call free_image to release.
uint8_t* load_image_rgb(const std::string& path, int* w, int* h);
void     free_image(uint8_t* p);

// Save a PNG with detections drawn on top of the original image.
// rgb : original decoded image (will be copied)
// w, h: original dimensions
// boxes: detection list in original-image coordinates
bool save_annotated_png(const std::string& out_path,
                        const uint8_t* rgb, int w, int h,
                        const std::vector<Box>& boxes);

struct RecResult;  // fwd

// Save detections as JSON. If `texts` is empty, only geometry is written.
bool save_json(const std::string& out_path,
               int img_w, int img_h,
               const std::vector<Box>& boxes,
               const std::vector<RecResult>& texts = {});

} // namespace uid
