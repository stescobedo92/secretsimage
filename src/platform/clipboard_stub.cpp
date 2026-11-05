// Fallback backend for platforms or sessions without a native clipboard
// implementation (for example Linux builds without X11 development files).
// File/CI mode keeps working; watch mode reports a clear error.
#include "secretsimage/clipboard.hpp"

namespace secretsimage {

ForegroundApp query_foreground_app() noexcept { return {}; }

Result<std::unique_ptr<IClipboard>> make_platform_clipboard() {
    return failure(ErrorCode::kUnsupportedPlatform,
                   "no clipboard backend is available in this build. On Linux, install the X11 "
                   "development package (libx11-dev) and rebuild; file and CI modes are unaffected");
}

}  // namespace secretsimage
