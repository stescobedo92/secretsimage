#include "secretsimage/redactor.hpp"

#include <algorithm>

namespace secretsimage {

std::vector<Box> merge_and_pad_boxes(std::vector<Box> boxes, int padding_px, int image_width,
                                     int image_height) {
    const int pad = std::max(0, padding_px);
    std::vector<Box> padded;
    padded.reserve(boxes.size());
    for (const Box& box : boxes) {
        if (box.empty()) continue;
        const int x0 = std::clamp(box.x - pad, 0, image_width);
        const int y0 = std::clamp(box.y - pad, 0, image_height);
        const int x1 = std::clamp(box.right() + pad, 0, image_width);
        const int y1 = std::clamp(box.bottom() + pad, 0, image_height);
        if (x1 > x0 && y1 > y0) padded.push_back(Box{x0, y0, x1 - x0, y1 - y0});
    }
    std::sort(padded.begin(), padded.end(), [](const Box& a, const Box& b) {
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });
    std::vector<Box> merged;
    for (const Box& box : padded) {
        bool absorbed = false;
        for (Box& kept : merged) {
            if (boxes_overlap(kept, box)) {
                kept = box_union(kept, box);
                absorbed = true;
                break;
            }
        }
        if (!absorbed) merged.push_back(box);
    }
    // A union can bridge two previously disjoint boxes; repeat until stable.
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < merged.size() && !changed; ++i) {
            for (std::size_t j = i + 1; j < merged.size(); ++j) {
                if (boxes_overlap(merged[i], merged[j])) {
                    merged[i] = box_union(merged[i], merged[j]);
                    merged.erase(merged.begin() + static_cast<std::ptrdiff_t>(j));
                    changed = true;
                    break;
                }
            }
        }
    }
    return merged;
}

std::vector<Box> redact_image(Image& image, const std::vector<Finding>& findings,
                              const RedactionOptions& options) {
    std::vector<Box> boxes;
    boxes.reserve(findings.size());
    for (const Finding& finding : findings) boxes.push_back(finding.box);

    std::vector<Box> painted;
    for (const Box& box :
         merge_and_pad_boxes(std::move(boxes), options.padding_px, image.width(), image.height())) {
        const Box done = image.fill_box(box, options.fill_color);
        if (!done.empty()) painted.push_back(done);
    }
    return painted;
}

}  // namespace secretsimage
