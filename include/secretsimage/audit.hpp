#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "secretsimage/detector.hpp"
#include "secretsimage/error.hpp"
#include "secretsimage/types.hpp"

namespace secretsimage {

// The audit record proves what was covered without preserving it: finding
// entries contain only kind, box and matched length. Image fingerprints
// let a reviewer confirm which exact bytes entered and left the tool.
struct AuditRecord {
    std::string tool_version;
    std::string timestamp_utc;  // ISO 8601, e.g. 2026-11-05T12:34:56Z
    std::string source;         // "file" or "clipboard"
    std::string input_label;    // file name only (no directory), or "clipboard"
    std::string output_label;   // file name only, or "clipboard"
    std::string foreground_app; // best-effort process name in watch mode, may be empty
    std::string foreground_window_title;  // may be empty; never includes image text
    int image_width = 0;
    int image_height = 0;
    std::string input_sha256;
    std::string output_sha256;
    long long elapsed_ms = 0;
    std::vector<Finding> findings;
    std::vector<Box> painted_boxes;
};

[[nodiscard]] std::string utc_now_iso8601();

// Serializes the record as JSON. Image-derived text is never written, and
// every emitted string is JSON-escaped.
[[nodiscard]] std::string audit_to_json(const AuditRecord& record);
[[nodiscard]] Result<void> write_audit_json(const std::filesystem::path& path,
                                            const AuditRecord& record);

}  // namespace secretsimage
