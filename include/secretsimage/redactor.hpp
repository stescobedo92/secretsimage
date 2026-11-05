#pragma once

#include <vector>

#include "secretsimage/detector.hpp"
#include "secretsimage/image.hpp"
#include "secretsimage/types.hpp"

namespace secretsimage {

struct RedactionOptions {
    // Irreversible solid fill. Blur and pixelation are never used: both can
    // be partially reversed. Black is the long-standing redaction convention.
    Color fill_color{0, 0, 0, 255};
    // Extra margin around every finding so anti-aliased glyph edges or a
    // slightly tight OCR box cannot leave a readable fringe at 200% zoom.
    int padding_px = 4;
};

// Paints every finding with the solid fill color, expanding each box by the
// configured padding and merging overlapping boxes first. Returns the exact
// clipped boxes that were painted (audit log + solid-fill verification).
std::vector<Box> redact_image(Image& image, const std::vector<Finding>& findings,
                              const RedactionOptions& options = {});

// Expands boxes by padding, clips to the image and merges overlaps into a
// minimal covering set. Exposed for tests and dry-run reporting.
[[nodiscard]] std::vector<Box> merge_and_pad_boxes(std::vector<Box> boxes, int padding_px,
                                                   int image_width, int image_height);

}  // namespace secretsimage
