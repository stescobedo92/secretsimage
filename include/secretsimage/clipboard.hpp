#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "secretsimage/error.hpp"
#include "secretsimage/image.hpp"
#include "secretsimage/ocr.hpp"
#include "secretsimage/pipeline.hpp"

namespace secretsimage {

// Best-effort identity of the application the user is currently working in
// (terminal, Word, Excel, PowerPoint, Teams, Slack, a browser showing
// Jira, ...). Used only for the audit log: interception happens at the
// clipboard, so every application that pastes the image receives the
// redacted version regardless of this value.
struct ForegroundApp {
    std::string process_name;   // e.g. "WINWORD.EXE", "Code", "firefox"
    std::string window_title;   // may be empty
};

[[nodiscard]] ForegroundApp query_foreground_app() noexcept;

// Platform clipboard holding at most one image. Implementations exist for
// Windows (Win32), macOS (NSPasteboard) and Linux/X11 (CLIPBOARD selection).
class IClipboard {
  public:
    virtual ~IClipboard() = default;
    IClipboard(const IClipboard&) = delete;
    IClipboard& operator=(const IClipboard&) = delete;
    IClipboard(IClipboard&&) = delete;
    IClipboard& operator=(IClipboard&&) = delete;

    // Monotonic counter bumped by the OS on every clipboard change, or 0
    // when the platform cannot provide one (the watcher then falls back
    // to hashing the image bytes).
    [[nodiscard]] virtual std::uint64_t change_sequence() = 0;
    [[nodiscard]] virtual Result<std::optional<Image>> read_image() = 0;
    [[nodiscard]] virtual Result<void> write_image(const Image& image) = 0;
    // Gives event-driven backends (X11 selection ownership) a chance to
    // serve paste requests for content written earlier. Called regularly
    // by the watcher loop; no-op on Windows and macOS.
    virtual void pump_events() {}

  protected:
    IClipboard() = default;
};

[[nodiscard]] Result<std::unique_ptr<IClipboard>> make_platform_clipboard();

struct WatchOptions {
    PipelineOptions pipeline;
    std::filesystem::path audit_log_path;  // JSON lines; empty disables file logging
    int poll_interval_ms = 200;
    int timeout_seconds = 0;  // 0 = run until stopped by signal/callback
    // Invoked with a human readable status line (counts and kinds only,
    // never secret text). Return false to stop watching.
    std::function<bool(const std::string&)> on_event;
};

// Watches the clipboard and replaces every new image with its redacted
// version before the user can paste it anywhere. Our own write is
// recognized by its SHA-256 fingerprint, so the redacted image is never
// re-processed. If redaction fails the clipboard is left untouched and the
// failure is reported through on_event (fail-closed logging, no silent pass).
class ClipboardWatcher {
  public:
    ClipboardWatcher(std::unique_ptr<IClipboard> clipboard, IOcrEngine& ocr_engine,
                     WatchOptions options);

    [[nodiscard]] Result<void> run();
    void stop() noexcept;

    // Processes one clipboard change if pending. Exposed for tests and for
    // hosts that own their own event loop. Returns true if an image was
    // seen (redacted or already ours/clean).
    [[nodiscard]] Result<bool> poll_once();

  private:
    std::unique_ptr<IClipboard> clipboard_;
    IOcrEngine& ocr_engine_;
    WatchOptions options_;
    std::uint64_t last_sequence_ = 0;
    bool have_sequence_ = false;
    std::string last_written_sha256_;
    std::string last_seen_sha256_;
    std::atomic<bool> stop_requested_{false};
};

}  // namespace secretsimage
