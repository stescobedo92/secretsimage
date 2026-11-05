// Windows clipboard backend and foreground-application detection.
// Covers every paste target at once (terminal, Word, Excel, PowerPoint,
// Teams, Slack, browsers showing Jira, ...): the clipboard itself is
// replaced, so Ctrl+V anywhere pastes the redacted image.
#include "secretsimage/clipboard.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <thread>

namespace secretsimage {
namespace {

std::string wide_to_utf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), size,
                        nullptr, nullptr);
    return out;
}

struct ClipboardGuard {
    bool opened = false;
    ClipboardGuard() {
        // Another application may hold the clipboard briefly; retry politely
        // instead of failing the interception.
        for (int attempt = 0; attempt < 10 && !opened; ++attempt) {
            opened = OpenClipboard(nullptr) != FALSE;
            if (!opened) std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }
    ~ClipboardGuard() {
        if (opened) CloseClipboard();
    }
    ClipboardGuard(const ClipboardGuard&) = delete;
    ClipboardGuard& operator=(const ClipboardGuard&) = delete;
};

Result<std::optional<Image>> image_from_dib(HANDLE handle) {
    const void* raw = GlobalLock(handle);
    if (raw == nullptr) return std::nullopt;
    struct Unlock {
        HANDLE handle;
        ~Unlock() { GlobalUnlock(handle); }
    } unlock{handle};

    const auto* info = static_cast<const BITMAPINFO*>(raw);
    const BITMAPINFOHEADER& header = info->bmiHeader;
    if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biPlanes != 1 ||
        header.biCompression != BI_RGB || (header.biBitCount != 24 && header.biBitCount != 32)) {
        return failure(ErrorCode::kClipboardError,
                       "clipboard DIB format is not supported (expected 24/32 bpp BI_RGB)");
    }
    const int width = header.biWidth;
    const int height_abs = header.biHeight < 0 ? -header.biHeight : header.biHeight;
    if (width <= 0 || height_abs <= 0) return std::nullopt;
    const bool top_down = header.biHeight < 0;
    const int bytes_per_pixel = header.biBitCount / 8;
    const std::size_t stride =
        ((static_cast<std::size_t>(width) * static_cast<std::size_t>(header.biBitCount) + 31U) /
         32U) *
        4U;
    const auto* base = static_cast<const std::uint8_t*>(raw);
    const std::uint8_t* bits = base + header.biSize;
    if (const std::size_t available = GlobalSize(handle);
        static_cast<std::size_t>(bits - base) +
                stride * static_cast<std::size_t>(height_abs) >
            available) {
        return failure(ErrorCode::kClipboardError, "clipboard DIB is truncated");
    }
    Image image(width, height_abs);
    bool any_alpha = false;
    for (int row = 0; row < height_abs; ++row) {
        const int source_row = top_down ? row : (height_abs - 1 - row);
        const std::uint8_t* src = bits + static_cast<std::size_t>(source_row) * stride;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t blue = src[static_cast<std::size_t>(x * bytes_per_pixel)];
            const std::uint8_t green = src[static_cast<std::size_t>(x * bytes_per_pixel) + 1];
            const std::uint8_t red = src[static_cast<std::size_t>(x * bytes_per_pixel) + 2];
            const std::uint8_t alpha =
                bytes_per_pixel == 4 ? src[static_cast<std::size_t>(x * bytes_per_pixel) + 3]
                                     : std::uint8_t{255};
            if (alpha != 0) any_alpha = true;
            image.set_pixel(x, row, Color{red, green, blue, alpha});
        }
    }
    if (bytes_per_pixel == 4 && !any_alpha) {
        auto pixels = image.pixels();
        for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    }
    return image;
}

HGLOBAL make_dib(const Image& image) {
    const std::size_t pixel_bytes =
        static_cast<std::size_t>(image.width()) * static_cast<std::size_t>(image.height()) * 4U;
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + pixel_bytes);
    if (handle == nullptr) return nullptr;
    auto* header = static_cast<BITMAPINFOHEADER*>(GlobalLock(handle));
    if (header == nullptr) {
        GlobalFree(handle);
        return nullptr;
    }
    *header = BITMAPINFOHEADER{};
    header->biSize = sizeof(BITMAPINFOHEADER);
    header->biWidth = image.width();
    header->biHeight = -image.height();  // top-down
    header->biPlanes = 1;
    header->biBitCount = 32;
    header->biCompression = BI_RGB;
    header->biSizeImage = static_cast<DWORD>(pixel_bytes);
    auto* bits = reinterpret_cast<std::uint8_t*>(header + 1);
    const auto pixels = image.pixels();
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        bits[i] = pixels[i + 2];      // B
        bits[i + 1] = pixels[i + 1];  // G
        bits[i + 2] = pixels[i];      // R
        bits[i + 3] = pixels[i + 3];  // A
    }
    GlobalUnlock(handle);
    return handle;
}

HGLOBAL make_blob(const std::vector<std::uint8_t>& bytes) {
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (handle == nullptr) return nullptr;
    void* target = GlobalLock(handle);
    if (target == nullptr) {
        GlobalFree(handle);
        return nullptr;
    }
    std::memcpy(target, bytes.data(), bytes.size());
    GlobalUnlock(handle);
    return handle;
}

// Converts a device-dependent CF_BITMAP (the only format some owners,
// notably .NET/WinForms, put on the clipboard) into our pixel format.
Result<std::optional<Image>> image_from_bitmap(HBITMAP bitmap) {
    BITMAP info{};
    if (GetObjectW(bitmap, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight <= 0) {
        return std::nullopt;
    }
    const int width = info.bmWidth;
    const int height = info.bmHeight;
    BITMAPINFO request{};
    request.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    request.bmiHeader.biWidth = width;
    request.bmiHeader.biHeight = -height;  // top-down BGRA
    request.bmiHeader.biPlanes = 1;
    request.bmiHeader.biBitCount = 32;
    request.bmiHeader.biCompression = BI_RGB;
    std::vector<std::uint8_t> bgra(static_cast<std::size_t>(width) *
                                   static_cast<std::size_t>(height) * 4U);
    HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
        return failure(ErrorCode::kClipboardError, "could not get a screen device context");
    }
    const int rows = GetDIBits(screen, bitmap, 0, static_cast<UINT>(height), bgra.data(), &request,
                               DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    if (rows == 0) {
        return failure(ErrorCode::kClipboardError, "GetDIBits failed for clipboard bitmap");
    }
    Image image(width, height);
    auto pixels = image.pixels();
    bool any_alpha = false;
    for (std::size_t i = 0; i + 3 < bgra.size(); i += 4) {
        pixels[i] = bgra[i + 2];
        pixels[i + 1] = bgra[i + 1];
        pixels[i + 2] = bgra[i];
        pixels[i + 3] = bgra[i + 3];
        if (bgra[i + 3] != 0) any_alpha = true;
    }
    if (!any_alpha) {
        for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    }
    return image;
}

class Win32Clipboard final : public IClipboard {
  public:
    std::uint64_t change_sequence() override { return GetClipboardSequenceNumber(); }

    Result<std::optional<Image>> read_image() override {
        ClipboardGuard guard;
        if (!guard.opened) {
            return failure(ErrorCode::kClipboardError, "could not open the Windows clipboard");
        }
        const UINT png_format = RegisterClipboardFormatW(L"PNG");
        if (png_format != 0) {
            if (HANDLE handle = GetClipboardData(png_format)) {
                if (const void* raw = GlobalLock(handle)) {
                    const std::size_t size = GlobalSize(handle);
                    std::vector<std::uint8_t> bytes(static_cast<const std::uint8_t*>(raw),
                                                    static_cast<const std::uint8_t*>(raw) + size);
                    GlobalUnlock(handle);
                    auto decoded = Image::decode(bytes);
                    if (decoded) return std::optional<Image>{std::move(*decoded)};
                }
            }
        }
        std::optional<Error> first_error;
        for (UINT format : {static_cast<UINT>(CF_DIB), static_cast<UINT>(CF_DIBV5)}) {
            if (HANDLE handle = GetClipboardData(format)) {
                auto dib = image_from_dib(handle);
                if (dib && dib->has_value()) return dib;
                if (!dib && !first_error) first_error = dib.error();
            }
        }
        if (auto bitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP))) {
            auto from_bitmap = image_from_bitmap(bitmap);
            if (from_bitmap) return from_bitmap;
            if (!first_error) first_error = from_bitmap.error();
        }
        if (first_error) return std::unexpected(*first_error);
        return std::optional<Image>{};
    }

    Result<void> write_image(const Image& image) override {
        auto png = image.encode(ImageFormat::kPng);
        if (!png) return std::unexpected(png.error());

        ClipboardGuard guard;
        if (!guard.opened) {
            return failure(ErrorCode::kClipboardError, "could not open the Windows clipboard");
        }
        if (!EmptyClipboard()) {
            return failure(ErrorCode::kClipboardError, "could not empty the Windows clipboard");
        }
        if (HGLOBAL dib = make_dib(image)) {
            if (SetClipboardData(CF_DIB, dib) == nullptr) GlobalFree(dib);
        }
        const UINT png_format = RegisterClipboardFormatW(L"PNG");
        if (png_format != 0) {
            if (HGLOBAL blob = make_blob(*png)) {
                if (SetClipboardData(png_format, blob) == nullptr) GlobalFree(blob);
            }
        }
        return {};
    }
};

}  // namespace

ForegroundApp query_foreground_app() noexcept {
    ForegroundApp app;
    try {
        HWND window = GetForegroundWindow();
        if (window == nullptr) return app;
        std::array<wchar_t, 512> title{};
        const int title_len = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
        if (title_len > 0) {
            app.window_title = wide_to_utf8(std::wstring(title.data(),
                                                         static_cast<std::size_t>(title_len)));
        }
        DWORD process_id = 0;
        GetWindowThreadProcessId(window, &process_id);
        if (process_id == 0) return app;
        HANDLE process =
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
        if (process == nullptr) return app;
        std::array<wchar_t, 1024> path{};
        DWORD size = static_cast<DWORD>(path.size());
        if (QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
            std::wstring full(path.data(), size);
            const std::size_t slash = full.find_last_of(L"\\/");
            if (slash != std::wstring::npos) full = full.substr(slash + 1);
            app.process_name = wide_to_utf8(full);
        }
        CloseHandle(process);
    } catch (...) {
        return app;  // best effort only; never blocks redaction
    }
    return app;
}

Result<std::unique_ptr<IClipboard>> make_platform_clipboard() {
    return std::make_unique<Win32Clipboard>();
}

}  // namespace secretsimage
