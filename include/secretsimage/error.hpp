#pragma once

#include <expected>
#include <string>
#include <utility>

namespace secretsimage {

enum class ErrorCode {
    kInvalidArgument,
    kIoError,
    kDecodeError,
    kEncodeError,
    kOcrUnavailable,
    kOcrFailed,
    kClipboardUnavailable,
    kClipboardError,
    kUnsupportedPlatform,
    kInternal,
};

[[nodiscard]] constexpr const char* to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::kInvalidArgument: return "invalid_argument";
        case ErrorCode::kIoError: return "io_error";
        case ErrorCode::kDecodeError: return "decode_error";
        case ErrorCode::kEncodeError: return "encode_error";
        case ErrorCode::kOcrUnavailable: return "ocr_unavailable";
        case ErrorCode::kOcrFailed: return "ocr_failed";
        case ErrorCode::kClipboardUnavailable: return "clipboard_unavailable";
        case ErrorCode::kClipboardError: return "clipboard_error";
        case ErrorCode::kUnsupportedPlatform: return "unsupported_platform";
        case ErrorCode::kInternal: return "internal_error";
    }
    return "internal_error";
}

// Error values must never carry secret material. Messages describe the
// failure only (paths, sizes, OS error text), never OCR text or findings.
struct Error {
    ErrorCode code = ErrorCode::kInternal;
    std::string message;

    Error() = default;
    Error(ErrorCode error_code, std::string error_message)
        : code(error_code), message(std::move(error_message)) {}
};

// All fallible core APIs return Result<T> instead of throwing across the
// public boundary. Callers in clipboard hooks can fail closed: on error the
// original image is left untouched and the failure is logged.
template <typename T>
using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> failure(ErrorCode code, std::string message) {
    return std::unexpected<Error>(Error{code, std::move(message)});
}

}  // namespace secretsimage
