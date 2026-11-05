#include "secretsimage/image.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>

#include "stb_image.h"
#include "stb_image_write.h"

namespace secretsimage {
namespace {

constexpr std::size_t kChannels = 4;
constexpr std::size_t kMaxPixels = 200'000'000;  // guard: 200 MP is far beyond any screenshot

Result<Image> image_from_stb(unsigned char* decoded, int width, int height) {
    if (decoded == nullptr || width <= 0 || height <= 0) {
        if (decoded != nullptr) stbi_image_free(decoded);
        return failure(ErrorCode::kDecodeError,
                       std::string{"image decode failed: "} + stbi_failure_reason());
    }
    const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixel_count > kMaxPixels) {
        stbi_image_free(decoded);
        return failure(ErrorCode::kDecodeError, "image exceeds the maximum supported size");
    }
    Image image(width, height);
    std::memcpy(image.pixels().data(), decoded, pixel_count * kChannels);
    stbi_image_free(decoded);
    return image;
}

void write_to_vector(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(context);
    const auto* first = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), first, first + static_cast<std::size_t>(size));
}

std::string lowercase_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return ext;
}

}  // namespace

Image::Image(int width, int height, Color fill) : width_(width), height_(height) {
    if (width_ <= 0 || height_ <= 0) {
        width_ = 0;
        height_ = 0;
        return;
    }
    pixels_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) *
                   kChannels);
    for (std::size_t i = 0; i + 3 < pixels_.size(); i += kChannels) {
        pixels_[i] = fill.r;
        pixels_[i + 1] = fill.g;
        pixels_[i + 2] = fill.b;
        pixels_[i + 3] = fill.a;
    }
}

Result<Image> Image::load_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return failure(ErrorCode::kIoError, "cannot open input file: " + path.string());
    }
    const auto end = in.tellg();
    if (end <= 0) {
        return failure(ErrorCode::kIoError, "input file is empty: " + path.string());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return failure(ErrorCode::kIoError, "cannot read input file: " + path.string());
    }
    return decode(bytes);
}

Result<Image> Image::decode(std::span<const std::uint8_t> encoded) {
    if (encoded.empty() || encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return failure(ErrorCode::kDecodeError, "encoded image data is empty or too large");
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* decoded =
        stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height,
                              &channels, static_cast<int>(kChannels));
    return image_from_stb(decoded, width, height);
}

Result<std::vector<std::uint8_t>> Image::encode(ImageFormat format, int jpeg_quality) const {
    if (empty()) {
        return failure(ErrorCode::kEncodeError, "cannot encode an empty image");
    }
    std::vector<std::uint8_t> out;
    out.reserve(pixels_.size() / 4U);
    const int stride = width_ * static_cast<int>(kChannels);
    int ok = 0;
    switch (format) {
        case ImageFormat::kPng:
            ok = stbi_write_png_to_func(&write_to_vector, &out, width_, height_,
                                        static_cast<int>(kChannels), pixels_.data(), stride);
            break;
        case ImageFormat::kJpeg:
            ok = stbi_write_jpg_to_func(&write_to_vector, &out, width_, height_,
                                        static_cast<int>(kChannels), pixels_.data(),
                                        std::clamp(jpeg_quality, 1, 100));
            break;
        case ImageFormat::kBmp:
            ok = stbi_write_bmp_to_func(&write_to_vector, &out, width_, height_,
                                        static_cast<int>(kChannels), pixels_.data());
            break;
    }
    if (ok == 0 || out.empty()) {
        return failure(ErrorCode::kEncodeError, "image encode failed");
    }
    return out;
}

Result<void> Image::save_file(const std::filesystem::path& path, int jpeg_quality) const {
    auto encoded = encode(format_from_extension(path), jpeg_quality);
    if (!encoded) return std::unexpected(encoded.error());

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return failure(ErrorCode::kIoError, "cannot create output directory: " + ec.message());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return failure(ErrorCode::kIoError, "cannot open output file: " + path.string());
    }
    out.write(reinterpret_cast<const char*>(encoded->data()),
              static_cast<std::streamsize>(encoded->size()));
    if (!out) {
        return failure(ErrorCode::kIoError, "cannot write output file: " + path.string());
    }
    return {};
}

Color Image::pixel_at(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return Color{};
    const auto offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) *
        kChannels;
    return Color{pixels_[offset], pixels_[offset + 1], pixels_[offset + 2], pixels_[offset + 3]};
}

void Image::set_pixel(int x, int y, Color color) noexcept {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    const auto offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) *
        kChannels;
    pixels_[offset] = color.r;
    pixels_[offset + 1] = color.g;
    pixels_[offset + 2] = color.b;
    pixels_[offset + 3] = color.a;
}

Box Image::fill_box(Box box, Color color) noexcept {
    const int x0 = std::clamp(box.x, 0, width_);
    const int y0 = std::clamp(box.y, 0, height_);
    const int x1 = std::clamp(box.right(), 0, width_);
    const int y1 = std::clamp(box.bottom(), 0, height_);
    if (x1 <= x0 || y1 <= y0) return Box{};
    for (int y = y0; y < y1; ++y) {
        auto offset =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x0)) *
            kChannels;
        for (int x = x0; x < x1; ++x) {
            pixels_[offset] = color.r;
            pixels_[offset + 1] = color.g;
            pixels_[offset + 2] = color.b;
            pixels_[offset + 3] = color.a;
            offset += kChannels;
        }
    }
    return Box{x0, y0, x1 - x0, y1 - y0};
}

bool Image::is_solid(Box box, Color color) const noexcept {
    const int x0 = std::clamp(box.x, 0, width_);
    const int y0 = std::clamp(box.y, 0, height_);
    const int x1 = std::clamp(box.right(), 0, width_);
    const int y1 = std::clamp(box.bottom(), 0, height_);
    if (x1 <= x0 || y1 <= y0) return false;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (!(pixel_at(x, y) == color)) return false;
        }
    }
    return true;
}

ImageFormat format_from_extension(const std::filesystem::path& path) noexcept {
    const std::string ext = lowercase_extension(path);
    if (ext == ".jpg" || ext == ".jpeg") return ImageFormat::kJpeg;
    if (ext == ".bmp") return ImageFormat::kBmp;
    return ImageFormat::kPng;  // lossless default: redaction must survive re-encoding exactly
}

}  // namespace secretsimage
