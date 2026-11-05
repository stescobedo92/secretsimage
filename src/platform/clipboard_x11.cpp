// Linux/X11 clipboard backend using the CLIPBOARD selection.
// Works under X11 and XWayland. Native Wayland sessions should run the
// tool under XWayland or use file/CI mode; see docs/USAGE.md.
//
// The backend owns the selection after writing the redacted image and
// answers SelectionRequest events from the watcher's pump loop, which is
// how X11 serves paste operations to every other application.
#include "secretsimage/clipboard.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstring>
#include <thread>

namespace secretsimage {
namespace {

class X11Clipboard final : public IClipboard {
  public:
    explicit X11Clipboard(Display* display) : display_(display) {
        window_ = XCreateSimpleWindow(display_, DefaultRootWindow(display_), 0, 0, 1, 1, 0, 0, 0);
        atom_clipboard_ = XInternAtom(display_, "CLIPBOARD", False);
        atom_png_ = XInternAtom(display_, "image/png", False);
        atom_targets_ = XInternAtom(display_, "TARGETS", False);
        atom_property_ = XInternAtom(display_, "SECRETSIMAGE_DATA", False);
        atom_incr_ = XInternAtom(display_, "INCR", False);
    }
    ~X11Clipboard() override {
        if (display_ != nullptr) {
            XDestroyWindow(display_, window_);
            XCloseDisplay(display_);
        }
    }

    // X11 has no global change counter; returning 0 makes the watcher fall
    // back to its image-fingerprint comparison.
    std::uint64_t change_sequence() override { return 0; }

    Result<std::optional<Image>> read_image() override {
        if (!owned_png_.empty() && XGetSelectionOwner(display_, atom_clipboard_) == window_) {
            auto decoded = Image::decode(owned_png_);
            if (decoded) return std::optional<Image>{std::move(*decoded)};
            return std::nullopt;
        }
        const Window owner = XGetSelectionOwner(display_, atom_clipboard_);
        if (owner == None) return std::optional<Image>{};

        XConvertSelection(display_, atom_clipboard_, atom_png_, atom_property_, window_,
                          CurrentTime);
        XFlush(display_);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        bool notified = false;
        bool refused = false;
        while (std::chrono::steady_clock::now() < deadline) {
            XEvent event{};
            if (XCheckTypedWindowEvent(display_, window_, SelectionNotify, &event)) {
                notified = true;
                refused = event.xselection.property == None;
                break;
            }
            pump_events();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!notified || refused) return std::optional<Image>{};  // no image/png on offer

        Atom actual_type = None;
        int actual_format = 0;
        unsigned long item_count = 0;
        unsigned long bytes_after = 0;
        unsigned char* data = nullptr;
        XGetWindowProperty(display_, window_, atom_property_, 0, 1L << 28, True, AnyPropertyType,
                           &actual_type, &actual_format, &item_count, &bytes_after, &data);
        if (data == nullptr) return std::optional<Image>{};
        if (actual_type == atom_incr_) {
            XFree(data);
            return failure(ErrorCode::kClipboardError,
                           "clipboard image uses the INCR transfer protocol (very large image), "
                           "which this backend does not support yet");
        }
        const std::size_t byte_count =
            item_count * static_cast<std::size_t>(actual_format == 0 ? 1 : actual_format / 8);
        std::vector<std::uint8_t> bytes(data, data + byte_count);
        XFree(data);
        auto decoded = Image::decode(bytes);
        if (!decoded) return std::unexpected(decoded.error());
        return std::optional<Image>{std::move(*decoded)};
    }

    Result<void> write_image(const Image& image) override {
        auto png = image.encode(ImageFormat::kPng);
        if (!png) return std::unexpected(png.error());
        owned_png_ = std::move(*png);
        XSetSelectionOwner(display_, atom_clipboard_, window_, CurrentTime);
        XFlush(display_);
        if (XGetSelectionOwner(display_, atom_clipboard_) != window_) {
            owned_png_.clear();
            return failure(ErrorCode::kClipboardError, "could not take ownership of the X11 CLIPBOARD selection");
        }
        return {};
    }

    void pump_events() override {
        while (XPending(display_) > 0) {
            XEvent event{};
            XNextEvent(display_, &event);
            if (event.type == SelectionClear) {
                owned_png_.clear();
            } else if (event.type == SelectionRequest) {
                serve_request(event.xselectionrequest);
            }
        }
    }

  private:
    void serve_request(const XSelectionRequestEvent& request) {
        XEvent reply{};
        reply.type = SelectionNotify;
        reply.xselection.display = request.display;
        reply.xselection.requestor = request.requestor;
        reply.xselection.selection = request.selection;
        reply.xselection.target = request.target;
        reply.xselection.time = request.time;
        reply.xselection.property = None;

        const Atom property = request.property == None ? request.target : request.property;
        if (request.target == atom_targets_) {
            const std::array<Atom, 2> targets{atom_targets_, atom_png_};
            XChangeProperty(display_, request.requestor, property, XA_ATOM, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(targets.data()),
                            static_cast<int>(targets.size()));
            reply.xselection.property = property;
        } else if (request.target == atom_png_ && !owned_png_.empty()) {
            XChangeProperty(display_, request.requestor, property, atom_png_, 8, PropModeReplace,
                            owned_png_.data(), static_cast<int>(owned_png_.size()));
            reply.xselection.property = property;
        }
        XSendEvent(display_, request.requestor, False, 0, &reply);
        XFlush(display_);
    }

    Display* display_ = nullptr;
    Window window_ = 0;
    Atom atom_clipboard_ = None;
    Atom atom_png_ = None;
    Atom atom_targets_ = None;
    Atom atom_property_ = None;
    Atom atom_incr_ = None;
    std::vector<std::uint8_t> owned_png_;
};

std::string read_window_text_property(Display* display, Window window, Atom property) {
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long item_count = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(display, window, property, 0, 4096, False, AnyPropertyType,
                           &actual_type, &actual_format, &item_count, &bytes_after, &data) !=
            Success ||
        data == nullptr) {
        return {};
    }
    std::string out(reinterpret_cast<const char*>(data),
                    item_count * static_cast<std::size_t>(actual_format == 0 ? 1 : actual_format / 8));
    XFree(data);
    const std::size_t nul = out.find('\0');
    if (nul != std::string::npos) {
        // WM_CLASS is "instance\0class": the class (application) part wins.
        const std::string tail = out.substr(nul + 1);
        return tail.empty() ? out.substr(0, nul) : std::string(tail.c_str());
    }
    return out;
}

}  // namespace

ForegroundApp query_foreground_app() noexcept {
    ForegroundApp app;
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) return app;
    const Window root = DefaultRootWindow(display);
    const Atom net_active = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long item_count = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    Window active = 0;
    if (net_active != None &&
        XGetWindowProperty(display, root, net_active, 0, 1, False, XA_WINDOW, &actual_type,
                           &actual_format, &item_count, &bytes_after, &data) == Success &&
        data != nullptr && item_count == 1) {
        std::memcpy(&active, data, sizeof(Window));
    }
    if (data != nullptr) XFree(data);
    if (active != 0) {
        app.window_title =
            read_window_text_property(display, active, XInternAtom(display, "_NET_WM_NAME", False));
        if (app.window_title.empty()) {
            app.window_title = read_window_text_property(display, active, XA_WM_NAME);
        }
        app.process_name = read_window_text_property(display, active, XA_WM_CLASS);
    }
    XCloseDisplay(display);
    return app;
}

Result<std::unique_ptr<IClipboard>> make_platform_clipboard() {
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return failure(ErrorCode::kClipboardUnavailable,
                       "could not connect to the X11 display. On a native Wayland session, run "
                       "with XWayland available or use file/CI mode");
    }
    return std::unique_ptr<IClipboard>(new X11Clipboard(display));
}

}  // namespace secretsimage
