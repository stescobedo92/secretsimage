// macOS clipboard backend using NSPasteboard. Replacing the pasteboard
// image means every application (Terminal, Word, Excel, PowerPoint, Teams,
// Slack, browsers, ...) pastes the redacted version.
#import <AppKit/AppKit.h>

#include "secretsimage/clipboard.hpp"

namespace secretsimage {
namespace {

class MacClipboard final : public IClipboard {
  public:
    std::uint64_t change_sequence() override {
        @autoreleasepool {
            return static_cast<std::uint64_t>([[NSPasteboard generalPasteboard] changeCount]);
        }
    }

    Result<std::optional<Image>> read_image() override {
        @autoreleasepool {
            NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
            NSData* png = [pasteboard dataForType:NSPasteboardTypePNG];
            if (png == nil) {
                // Many apps publish TIFF instead of PNG; normalize via NSImage.
                NSImage* ns_image = [[NSImage alloc] initWithPasteboard:pasteboard];
                if (ns_image == nil) return std::optional<Image>{};
                NSData* tiff = [ns_image TIFFRepresentation];
                if (tiff == nil) return std::optional<Image>{};
                NSBitmapImageRep* rep = [NSBitmapImageRep imageRepWithData:tiff];
                if (rep == nil) return std::optional<Image>{};
                png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                if (png == nil) return std::optional<Image>{};
            }
            std::vector<std::uint8_t> bytes(static_cast<const std::uint8_t*>([png bytes]),
                                            static_cast<const std::uint8_t*>([png bytes]) +
                                                [png length]);
            auto decoded = Image::decode(bytes);
            if (!decoded) return std::unexpected(decoded.error());
            return std::optional<Image>{std::move(*decoded)};
        }
    }

    Result<void> write_image(const Image& image) override {
        auto png = image.encode(ImageFormat::kPng);
        if (!png) return std::unexpected(png.error());
        @autoreleasepool {
            NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
            [pasteboard clearContents];
            NSData* data = [NSData dataWithBytes:png->data() length:png->size()];
            if (![pasteboard setData:data forType:NSPasteboardTypePNG]) {
                return failure(ErrorCode::kClipboardError, "could not write PNG data to NSPasteboard");
            }
        }
        return {};
    }
};

std::string ns_to_std(NSString* value) {
    if (value == nil) return {};
    const char* utf8 = [value UTF8String];
    return utf8 == nullptr ? std::string{} : std::string{utf8};
}

}  // namespace

ForegroundApp query_foreground_app() noexcept {
    ForegroundApp app;
    @autoreleasepool {
        NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (front != nil) {
            app.process_name = ns_to_std([front localizedName]);
            if (app.process_name.empty()) app.process_name = ns_to_std([front bundleIdentifier]);
        }
        // The frontmost window title requires Accessibility permission, so it
        // is intentionally left empty rather than prompting the user.
    }
    return app;
}

Result<std::unique_ptr<IClipboard>> make_platform_clipboard() {
    return std::make_unique<MacClipboard>();
}

}  // namespace secretsimage
