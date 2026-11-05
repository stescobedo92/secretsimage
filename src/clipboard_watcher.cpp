#include "secretsimage/clipboard.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

#include "secretsimage/audit.hpp"
#include "secretsimage/sha256.hpp"
#include "secretsimage/version.hpp"

namespace secretsimage {
namespace {

std::string describe_findings(const std::vector<Finding>& findings) {
    std::ostringstream out;
    out << findings.size() << " finding(s):";
    bool first = true;
    for (const Finding& finding : findings) {
        out << (first ? " " : ", ") << to_string(finding.kind);
        first = false;
    }
    return out.str();
}

void append_audit_line(const std::filesystem::path& path, const AuditRecord& record) {
    // JSON Lines: the pretty-printed record with whitespace collapsed.
    std::string json = audit_to_json(record);
    std::string compact;
    compact.reserve(json.size());
    for (char ch : json) compact += (ch == '\n' ? ' ' : ch);
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (out) out << compact << '\n';
}

}  // namespace

ClipboardWatcher::ClipboardWatcher(std::unique_ptr<IClipboard> clipboard, IOcrEngine& ocr_engine,
                                   WatchOptions options)
    : clipboard_(std::move(clipboard)), ocr_engine_(ocr_engine), options_(std::move(options)) {}

void ClipboardWatcher::stop() noexcept { stop_requested_.store(true, std::memory_order_relaxed); }

Result<bool> ClipboardWatcher::poll_once() {
    clipboard_->pump_events();
    const std::uint64_t sequence = clipboard_->change_sequence();
    if (sequence != 0) {
        if (have_sequence_ && sequence == last_sequence_) return false;
    }

    auto image = clipboard_->read_image();
    if (!image) return std::unexpected(image.error());
    last_sequence_ = sequence;
    have_sequence_ = true;
    if (!image->has_value()) return false;

    Image working = std::move(image->value());
    const std::string fingerprint = sha256_hex(working.pixels());
    if (fingerprint == last_written_sha256_) return true;  // our own redacted write
    if (sequence == 0 && fingerprint == last_seen_sha256_) return false;

    const std::string input_hash = fingerprint;
    const int width = working.width();
    const int height = working.height();
    const ForegroundApp app = query_foreground_app();

    auto result = redact_with_ocr(working, ocr_engine_, options_.pipeline);
    if (!result) {
        last_seen_sha256_ = fingerprint;  // do not retry the same image forever
        // The caller (run loop or embedder) reports the returned error; the
        // clipboard was left unchanged so no unredacted paste is hidden.
        return std::unexpected(result.error());
    }
    last_seen_sha256_ = fingerprint;

    if (!result->has_secrets()) {
        if (options_.on_event) {
            std::ostringstream message;
            message << "clean image " << width << 'x' << height << " left on clipboard ("
                    << result->elapsed_ms << " ms)";
            if (!app.process_name.empty()) message << " [app: " << app.process_name << ']';
            if (!options_.on_event(message.str())) stop();
        }
        return true;
    }

    auto written = clipboard_->write_image(working);
    if (!written) return std::unexpected(written.error());
    last_written_sha256_ = sha256_hex(working.pixels());
    last_seen_sha256_ = last_written_sha256_;
    last_sequence_ = clipboard_->change_sequence();

    if (!options_.audit_log_path.empty()) {
        AuditRecord record;
        record.tool_version = kVersion;
        record.timestamp_utc = utc_now_iso8601();
        record.source = "clipboard";
        record.input_label = "clipboard";
        record.output_label = "clipboard";
        record.foreground_app = app.process_name;
        record.foreground_window_title = app.window_title;
        record.image_width = width;
        record.image_height = height;
        record.input_sha256 = input_hash;
        record.output_sha256 = last_written_sha256_;
        record.elapsed_ms = result->elapsed_ms;
        record.findings = result->findings;
        record.painted_boxes = result->painted_boxes;
        append_audit_line(options_.audit_log_path, record);
    }
    if (options_.on_event) {
        std::ostringstream message;
        message << "REDACTED clipboard image " << width << 'x' << height << ": "
                << describe_findings(result->findings) << " (" << result->elapsed_ms << " ms)";
        if (!app.process_name.empty()) message << " [app: " << app.process_name << ']';
        if (!options_.on_event(message.str())) stop();
    }
    return true;
}

Result<void> ClipboardWatcher::run() {
    const auto deadline =
        options_.timeout_seconds > 0
            ? std::chrono::steady_clock::now() + std::chrono::seconds(options_.timeout_seconds)
            : std::chrono::steady_clock::time_point::max();
    const int poll_ms = std::max(50, options_.poll_interval_ms);
    while (!stop_requested_.load(std::memory_order_relaxed)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        auto polled = poll_once();
        if (!polled) {
            if (options_.on_event == nullptr) {
                // Without an event sink there is nobody to warn: abort loudly.
                return std::unexpected(polled.error());
            }
            options_.on_event("ERROR: clipboard poll failed: " + polled.error().message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
    }
    return {};
}

}  // namespace secretsimage
