#include <algorithm>
#include <array>
#include <cstdint>

#include "secretsimage/image.hpp"
#include "secretsimage/redactor.hpp"
#include "secretsimage/sha256.hpp"
#include "test_framework.hpp"

namespace {
using namespace secretsimage;  // NOLINT -- test translation unit
}

TEST_CASE("sha256 matches published test vectors") {
    const std::array<std::uint8_t, 0> empty{};
    CHECK(sha256_hex(empty) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string abc = "abc";
    CHECK(sha256_hex(std::span<const std::uint8_t>(
              reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size())) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("png roundtrip preserves pixels exactly") {
    Image image(17, 9);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.set_pixel(x, y, Color{static_cast<std::uint8_t>(x * 7),
                                        static_cast<std::uint8_t>(y * 19),
                                        static_cast<std::uint8_t>(x + y), 255});
        }
    }
    auto encoded = image.encode(ImageFormat::kPng);
    CHECK(encoded.has_value());
    auto decoded = Image::decode(*encoded);
    CHECK(decoded.has_value());
    CHECK(decoded->width() == 17 && decoded->height() == 9);
    CHECK(std::ranges::equal(decoded->pixels(), image.pixels()));
}

TEST_CASE("jpeg encoding decodes back with the same dimensions") {
    const Image image(40, 24, Color{200, 30, 30, 255});
    auto encoded = image.encode(ImageFormat::kJpeg, 60);
    CHECK(encoded.has_value());
    auto decoded = Image::decode(*encoded);
    CHECK(decoded.has_value());
    CHECK(decoded->width() == 40 && decoded->height() == 24);
}

TEST_CASE("decode rejects garbage without crashing") {
    const std::vector<std::uint8_t> junk{0x00, 0x11, 0x22, 0x33, 0x44};
    CHECK(!Image::decode(junk).has_value());
    CHECK(!Image::decode({}).has_value());
}

TEST_CASE("fill_box clips to the image and paints one solid color") {
    Image image(20, 10, Color{255, 255, 255, 255});
    const Box painted = image.fill_box(Box{-5, -5, 14, 9}, Color{0, 0, 0, 255});
    CHECK(painted == Box(0, 0, 9, 4));
    CHECK(image.is_solid(painted, Color{0, 0, 0, 255}));
    CHECK(image.pixel_at(19, 9) == Color(255, 255, 255, 255));
    CHECK(image.fill_box(Box{50, 50, 4, 4}, Color{0, 0, 0, 255}).empty());
}

TEST_CASE("merge_and_pad merges overlaps and clips") {
    const std::vector<Box> merged =
        merge_and_pad_boxes({Box{10, 10, 10, 10}, Box{18, 12, 10, 10}, Box{90, 90, 30, 30}}, 4,
                            100, 100);
    CHECK(merged.size() == 2);
    CHECK(merged[0] == Box(6, 6, 26, 20));
    CHECK(merged[1] == Box(86, 86, 14, 14));
}

TEST_CASE("redact_image is irreversible solid fill and touches nothing else") {
    Image image(30, 12, Color{255, 255, 255, 255});
    image.set_pixel(12, 5, Color{255, 0, 0, 255});
    std::vector<Finding> findings{Finding{SecretKind::kEmail, Box{10, 3, 8, 5}, 8}};
    const std::vector<Box> painted = redact_image(image, findings, RedactionOptions{});
    CHECK(painted.size() == 1);
    CHECK(image.is_solid(painted[0], Color{0, 0, 0, 255}));
    CHECK(image.pixel_at(12, 5) == Color(0, 0, 0, 255));  // original glyph pixel is gone
    CHECK(image.pixel_at(0, 0) == Color(255, 255, 255, 255));
}

TEST_CASE("format_from_extension picks the codec") {
    CHECK(format_from_extension("a.png") == ImageFormat::kPng);
    CHECK(format_from_extension("a.JPG") == ImageFormat::kJpeg);
    CHECK(format_from_extension("a.bmp") == ImageFormat::kBmp);
    CHECK(format_from_extension("a.unknownext") == ImageFormat::kPng);
}
