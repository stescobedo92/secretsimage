#pragma once

#include <filesystem>
#include <vector>

#include "secretsimage/audit.hpp"
#include "secretsimage/detector.hpp"
#include "secretsimage/error.hpp"
#include "secretsimage/image.hpp"
#include "secretsimage/ocr.hpp"
#include "secretsimage/redactor.hpp"

namespace secretsimage {

struct PipelineOptions {
    DetectorConfig detector;
    RedactionOptions redaction;
    OcrOptions ocr;
};

struct PipelineResult {
    std::vector<Finding> findings;
    std::vector<Box> painted_boxes;
    long long elapsed_ms = 0;
    [[nodiscard]] bool has_secrets() const noexcept { return !findings.empty(); }
};

// OCR + detection only. When options.ocr.upscale_factor > 1 the OCR runs
// at native scale AND upscaled, and the findings are merged: each scale
// recovers text the other mangles (measured on the synthetic matrix).
[[nodiscard]] Result<std::vector<Finding>> detect_with_ocr(const Image& image,
                                                           IOcrEngine& ocr_engine,
                                                           const PipelineOptions& options = {});

// OCR -> detect -> solid-fill, in place. On any error the image is left
// unmodified so callers can fail closed (block the paste/upload) instead
// of silently passing the original through.
[[nodiscard]] Result<PipelineResult> redact_with_ocr(Image& image, IOcrEngine& ocr_engine,
                                                     const PipelineOptions& options = {});

// Same pipeline with caller-provided OCR words. Used by the metrics harness
// with ground-truth words and by embedders that run their own OCR.
[[nodiscard]] PipelineResult redact_with_words(Image& image, const OcrResult& ocr,
                                               const PipelineOptions& options = {});

// File-level convenience used by the CLI and CI mode. When report_path is
// not empty an audit JSON is written next to the output. The input file is
// never modified; output_path must differ from input_path.
[[nodiscard]] Result<PipelineResult> redact_file(const std::filesystem::path& input_path,
                                                 const std::filesystem::path& output_path,
                                                 const std::filesystem::path& report_path,
                                                 IOcrEngine& ocr_engine,
                                                 const PipelineOptions& options = {});

}  // namespace secretsimage
