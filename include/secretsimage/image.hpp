#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "secretsimage/error.hpp"
#include "secretsimage/types.hpp"

namespace secretsimage {

enum class ImageFormat { kPng, kJpeg, kBmp };

// In-memory RGBA8 image, row-major, top row first, tightly packed
// (stride == width * 4). Decoding drops all container metadata (EXIF, text
// chunks, profiles) because only pixels are kept; re-encoding therefore
// cannot leak metadata that was present in the input file.
class Image {
  public:
    Image() = default;
    Image(int width, int height, Color fill = Color{0, 0, 0, 255});

    [[nodiscard]] static Result<Image> load_file(const std::filesystem::path& path);
    [[nodiscard]] static Result<Image> decode(std::span<const std::uint8_t> encoded);

    [[nodiscard]] Result<std::vector<std::uint8_t>> encode(ImageFormat format,
                                                           int jpeg_quality = 90) const;
    [[nodiscard]] Result<void> save_file(const std::filesystem::path& path,
                                         int jpeg_quality = 90) const;

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] bool empty() const noexcept { return width_ <= 0 || height_ <= 0; }
    [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept { return pixels_; }
    [[nodiscard]] std::span<std::uint8_t> pixels() noexcept { return pixels_; }

    [[nodiscard]] Color pixel_at(int x, int y) const noexcept;
    void set_pixel(int x, int y, Color color) noexcept;

    // Fills the intersection of box with the image with a single solid
    // color. Returns the clipped box that was actually painted.
    Box fill_box(Box box, Color color) noexcept;

    // True when every pixel inside box (clipped to the image) equals color.
    [[nodiscard]] bool is_solid(Box box, Color color) const noexcept;

  private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> pixels_;
};

[[nodiscard]] ImageFormat format_from_extension(const std::filesystem::path& path) noexcept;

}  // namespace secretsimage
