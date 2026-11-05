#include "secretsimage/pipeline.hpp"

#include <chrono>
#include <fstream>
#include <vector>

#include "secretsimage/audit.hpp"
#include "secretsimage/sha256.hpp"
#include "secretsimage/version.hpp"

namespace secretsimage {
namespace {

[[nodiscard]] long long elapsed_ms_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
}

[[nodiscard]] Result<std::vector<std::uint8_t>> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return failure(ErrorCode::kIoError, "cannot open input file: " + path.string());
    const auto end = in.tellg();
    if (end <= 0) return failure(ErrorCode::kIoError, "input file is empty: " + path.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()))) {
        return failure(ErrorCode::kIoError, "cannot read input file: " + path.string());
    }
    return bytes;
}

}  // namespace

PipelineResult redact_with_words(Image& image, const OcrResult& ocr, const PipelineOptions& options) {
    const auto start = std::chrono::steady_clock::now();
    PipelineResult result;
    result.findings = detect_secrets(ocr, options.detector);
    result.painted_boxes = redact_image(image, result.findings, options.redaction);
    result.elapsed_ms = elapsed_ms_since(start);
    return result;
}

Result<std::vector<Finding>> detect_with_ocr(const Image& image, IOcrEngine& ocr_engine,
                                             const PipelineOptions& options) {
    std::vector<int> scales{1};
    if (options.ocr.upscale_factor > 1) scales.push_back(options.ocr.upscale_factor);
    std::vector<Finding> merged;
    for (int scale : scales) {
        OcrOptions ocr_options = options.ocr;
        ocr_options.upscale_factor = scale;
        auto ocr = ocr_engine.recognize(image, ocr_options);
        if (!ocr) return std::unexpected(ocr.error());
        std::vector<Finding> pass = detect_secrets(*ocr, options.detector);
        merged.insert(merged.end(), pass.begin(), pass.end());
    }
    return dedupe_findings(std::move(merged));
}

Result<PipelineResult> redact_with_ocr(Image& image, IOcrEngine& ocr_engine,
                                       const PipelineOptions& options) {
    const auto start = std::chrono::steady_clock::now();
    auto findings = detect_with_ocr(image, ocr_engine, options);
    if (!findings) return std::unexpected(findings.error());
    PipelineResult result;
    result.findings = std::move(*findings);
    result.painted_boxes = redact_image(image, result.findings, options.redaction);
    result.elapsed_ms = elapsed_ms_since(start);
    return result;
}

Result<PipelineResult> redact_file(const std::filesystem::path& input_path,
                                   const std::filesystem::path& output_path,
                                   const std::filesystem::path& report_path,
                                   IOcrEngine& ocr_engine, const PipelineOptions& options) {
    std::error_code ec;
    const auto canonical_input = std::filesystem::weakly_canonical(input_path, ec);
    ec.clear();
    const auto canonical_output = std::filesystem::weakly_canonical(output_path, ec);
    if (!canonical_input.empty() && canonical_input == canonical_output) {
        return failure(ErrorCode::kInvalidArgument,
                       "output path must differ from input path: the original is never modified");
    }
    auto input_bytes = read_bytes(input_path);
    if (!input_bytes) return std::unexpected(input_bytes.error());
    auto image = Image::decode(*input_bytes);
    if (!image) return std::unexpected(image.error());

    const std::string input_hash = sha256_hex(*input_bytes);
    const int width = image->width();
    const int height = image->height();

    auto result = redact_with_ocr(*image, ocr_engine, options);
    if (!result) return std::unexpected(result.error());

    auto encoded = image->encode(format_from_extension(output_path));
    if (!encoded) return std::unexpected(encoded.error());
    {
        if (output_path.has_parent_path()) {
            std::filesystem::create_directories(output_path.parent_path(), ec);
            if (ec) {
                return failure(ErrorCode::kIoError,
                               "cannot create output directory: " + ec.message());
            }
        }
        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return failure(ErrorCode::kIoError, "cannot open output file: " + output_path.string());
        }
        out.write(reinterpret_cast<const char*>(encoded->data()),
                  static_cast<std::streamsize>(encoded->size()));
        if (!out) {
            return failure(ErrorCode::kIoError, "cannot write output file: " + output_path.string());
        }
    }

    if (!report_path.empty()) {
        AuditRecord record;
        record.tool_version = kVersion;
        record.timestamp_utc = utc_now_iso8601();
        record.source = "file";
        record.input_label = input_path.filename().string();
        record.output_label = output_path.filename().string();
        record.image_width = width;
        record.image_height = height;
        record.input_sha256 = input_hash;
        record.output_sha256 = sha256_hex(*encoded);
        record.elapsed_ms = result->elapsed_ms;
        record.findings = result->findings;
        record.painted_boxes = result->painted_boxes;
        if (auto written = write_audit_json(report_path, record); !written) {
            return std::unexpected(written.error());
        }
    }
    return result;
}

}  // namespace secretsimage
