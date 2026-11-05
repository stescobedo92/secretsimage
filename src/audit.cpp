#include "secretsimage/audit.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace secretsimage {
namespace {

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char raw : text) {
        const auto ch = static_cast<unsigned char>(raw);
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20U) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(ch));
                    out += buffer;
                } else {
                    out += static_cast<char>(ch);
                }
        }
    }
    return out;
}

void append_box(std::ostringstream& out, const char* key, Box box) {
    out << '"' << key << "\":{\"x\":" << box.x << ",\"y\":" << box.y << ",\"width\":" << box.width
        << ",\"height\":" << box.height << '}';
}

}  // namespace

std::string utc_now_iso8601() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &now) != 0) return {};
#else
    if (gmtime_r(&now, &utc) == nullptr) return {};
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) return {};
    return buffer;
}

std::string audit_to_json(const AuditRecord& record) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"tool\": \"secretsimage\",\n";
    out << "  \"version\": \"" << json_escape(record.tool_version) << "\",\n";
    out << "  \"timestamp_utc\": \"" << json_escape(record.timestamp_utc) << "\",\n";
    out << "  \"source\": \"" << json_escape(record.source) << "\",\n";
    out << "  \"input\": \"" << json_escape(record.input_label) << "\",\n";
    out << "  \"output\": \"" << json_escape(record.output_label) << "\",\n";
    out << "  \"foreground_app\": \"" << json_escape(record.foreground_app) << "\",\n";
    out << "  \"foreground_window_title\": \"" << json_escape(record.foreground_window_title)
        << "\",\n";
    out << "  \"image_width\": " << record.image_width << ",\n";
    out << "  \"image_height\": " << record.image_height << ",\n";
    out << "  \"input_sha256\": \"" << json_escape(record.input_sha256) << "\",\n";
    out << "  \"output_sha256\": \"" << json_escape(record.output_sha256) << "\",\n";
    out << "  \"elapsed_ms\": " << record.elapsed_ms << ",\n";
    out << "  \"note\": \"secret values are never logged; only kind, box and length\",\n";
    out << "  \"findings\": [";
    for (std::size_t i = 0; i < record.findings.size(); ++i) {
        const Finding& finding = record.findings[i];
        if (i != 0) out << ',';
        out << "\n    {\"kind\": \"" << to_string(finding.kind)
            << "\", \"matched_length\": " << finding.matched_length << ", ";
        append_box(out, "box", finding.box);
        out << '}';
    }
    out << (record.findings.empty() ? "],\n" : "\n  ],\n");
    out << "  \"painted_boxes\": [";
    for (std::size_t i = 0; i < record.painted_boxes.size(); ++i) {
        if (i != 0) out << ',';
        out << "\n    {";
        append_box(out, "box", record.painted_boxes[i]);
        out << '}';
    }
    out << (record.painted_boxes.empty() ? "]\n" : "\n  ]\n");
    out << "}\n";
    return out.str();
}

Result<void> write_audit_json(const std::filesystem::path& path, const AuditRecord& record) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return failure(ErrorCode::kIoError, "cannot create report directory: " + ec.message());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return failure(ErrorCode::kIoError, "cannot open report file: " + path.string());
    out << audit_to_json(record);
    if (!out) return failure(ErrorCode::kIoError, "cannot write report file: " + path.string());
    return {};
}

}  // namespace secretsimage
