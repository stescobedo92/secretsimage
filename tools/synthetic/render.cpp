// Screenshot renderer for synthetic cases. Primary path rasterizes the
// vendored DejaVu Sans Mono TrueType font at an exact pixel height so the
// PR-3/PR-4 matrix exercises OCR the way real terminals/editors look.
// If the font file cannot be found, a public-domain 8x8 bitmap font is
// used instead so tests never hard-depend on an external file.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"  // NOLINT
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include "synthetic.hpp"

#ifndef SYNTHETIC_FONT_DIR
#define SYNTHETIC_FONT_DIR "tools/synthetic/fonts"
#endif

// Defined by font8x8_basic.h inside synthetic.cpp (single TU upstream).
extern unsigned char font8x8_basic[128][8];  // NOLINT(modernize-avoid-c-arrays)

namespace synthetic {
namespace {

using secretsimage::Box;
using secretsimage::Color;

struct TrueTypeFont {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
    bool loaded = false;
};

[[nodiscard]] std::vector<unsigned char> read_binary(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto size = in.tellg();
    if (size <= 0) return {};
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) return {};
    return bytes;
}

[[nodiscard]] const TrueTypeFont& load_font() {
    static const TrueTypeFont font = [] {
        TrueTypeFont result;
        std::vector<std::filesystem::path> candidates;
        if (const char* env = std::getenv("SECRETSIMAGE_SYNTHETIC_FONT");
            env != nullptr && env[0] != '\0') {
            candidates.emplace_back(env);
        }
        candidates.emplace_back(std::filesystem::path(SYNTHETIC_FONT_DIR) / "DejaVuSansMono.ttf");
        candidates.emplace_back("tools/synthetic/fonts/DejaVuSansMono.ttf");
        for (const std::filesystem::path& candidate : candidates) {
            result.bytes = read_binary(candidate);
            if (result.bytes.empty()) continue;
            if (stbtt_InitFont(&result.info, result.bytes.data(),
                               stbtt_GetFontOffsetForIndex(result.bytes.data(), 0)) != 0) {
                result.loaded = true;
                break;
            }
        }
        return result;
    }();
    return font;
}

struct LineLayout {
    int line_height = 1;
    int text_height = 1;  // ascent..descent band used for word boxes
    std::vector<std::vector<float>> char_x;  // per line, x offset of each char + end sentinel
};

[[nodiscard]] Box box_for_range(const LineLayout& layout, const RenderOptions& options,
                                std::size_t line, std::size_t begin, std::size_t end) {
    const std::vector<float>& xs = layout.char_x[line];
    const int x0 = options.margin_px + static_cast<int>(std::lround(xs[begin]));
    const int x1 = options.margin_px + static_cast<int>(std::lround(xs[end]));
    const int y = options.margin_px + static_cast<int>(line) * layout.line_height;
    return Box{x0, y, std::max(1, x1 - x0), layout.text_height};
}

void add_ground_truth(RenderedCase& rendered, const SyntheticCase& test_case,
                      const LineLayout& layout, const RenderOptions& options) {
    for (std::size_t line_index = 0; line_index < test_case.lines.size(); ++line_index) {
        const std::string& line = test_case.lines[line_index];
        std::size_t pos = 0;
        while (pos < line.size()) {
            if (line[pos] == ' ') { ++pos; continue; }
            const std::size_t begin = pos;
            while (pos < line.size() && line[pos] != ' ') ++pos;
            secretsimage::OcrWord word;
            word.text = line.substr(begin, pos - begin);
            word.box = box_for_range(layout, options, line_index, begin, pos);
            word.confidence = 99.0F;
            word.block = 1;
            word.paragraph = 1;
            word.line = static_cast<int>(line_index) + 1;
            bool secret = false;
            for (const SecretSpan& span : test_case.secrets) {
                if (span.line == line_index && begin < span.end && span.begin < pos) secret = true;
            }
            rendered.word_is_secret.push_back(secret);
            rendered.ground_truth.words.push_back(std::move(word));
        }
    }
    rendered.secret_boxes.reserve(test_case.secrets.size());
    for (const SecretSpan& span : test_case.secrets) {
        rendered.secret_boxes.push_back(box_for_range(layout, options, span.line, span.begin, span.end));
    }
}

RenderedCase render_truetype(const SyntheticCase& test_case, const RenderOptions& options,
                             const TrueTypeFont& font, Color background, Color foreground) {
    const float pixel_height = static_cast<float>(std::clamp(options.font_px, 6, 72));
    const float scale = stbtt_ScaleForPixelHeight(&font.info, pixel_height);
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&font.info, &ascent, &descent, &line_gap);

    LineLayout layout;
    layout.text_height =
        std::max(1, static_cast<int>(std::lround(static_cast<float>(ascent - descent) * scale)));
    layout.line_height = std::max(
        layout.text_height,
        static_cast<int>(std::lround(static_cast<float>(ascent - descent + line_gap) * scale)));
    layout.char_x.resize(test_case.lines.size());
    float max_width = 0.0F;
    for (std::size_t i = 0; i < test_case.lines.size(); ++i) {
        const std::string& line = test_case.lines[i];
        layout.char_x[i].reserve(line.size() + 1);
        float x = 0.0F;
        for (char raw : line) {
            layout.char_x[i].push_back(x);
            int advance = 0, lsb = 0;
            const auto code = static_cast<unsigned char>(raw);
            stbtt_GetCodepointHMetrics(&font.info, code < 32 ? ' ' : code, &advance, &lsb);
            x += static_cast<float>(advance) * scale;
        }
        layout.char_x[i].push_back(x);
        max_width = std::max(max_width, x);
    }

    RenderedCase rendered;
    const int width = options.margin_px * 2 + static_cast<int>(std::ceil(max_width)) + 2;
    const int height = options.margin_px * 2 +
                       static_cast<int>(test_case.lines.size()) * layout.line_height + 2;
    rendered.image = secretsimage::Image(width, height, background);
    rendered.ground_truth.image_width = width;
    rendered.ground_truth.image_height = height;

    for (std::size_t line_index = 0; line_index < test_case.lines.size(); ++line_index) {
        const std::string& line = test_case.lines[line_index];
        const int baseline = options.margin_px +
                             static_cast<int>(line_index) * layout.line_height +
                             static_cast<int>(std::lround(static_cast<float>(ascent) * scale));
        for (std::size_t col = 0; col < line.size(); ++col) {
            const auto code = static_cast<unsigned char>(line[col]);
            if (code < 32 || code >= 127) continue;
            int gw = 0, gh = 0, xoff = 0, yoff = 0;
            unsigned char* bitmap =
                stbtt_GetCodepointBitmap(&font.info, scale, scale, code, &gw, &gh, &xoff, &yoff);
            if (bitmap == nullptr) continue;
            const int origin_x = options.margin_px +
                                 static_cast<int>(std::lround(layout.char_x[line_index][col])) + xoff;
            const int origin_y = baseline + yoff;
            for (int row = 0; row < gh; ++row) {
                for (int col_px = 0; col_px < gw; ++col_px) {
                    const auto alpha =
                        static_cast<int>(bitmap[static_cast<std::size_t>(row * gw + col_px)]);
                    if (alpha == 0) continue;
                    const int px = origin_x + col_px;
                    const int py = origin_y + row;
                    if (px < 0 || py < 0 || px >= width || py >= height) continue;
                    const Color current = rendered.image.pixel_at(px, py);
                    auto blend = [alpha](int fg, int bg) {
                        return static_cast<std::uint8_t>((fg * alpha + bg * (255 - alpha)) / 255);
                    };
                    rendered.image.set_pixel(
                        px, py,
                        Color{blend(foreground.r, current.r), blend(foreground.g, current.g),
                              blend(foreground.b, current.b), 255});
                }
            }
            stbtt_FreeBitmap(bitmap, nullptr);
        }
    }
    add_ground_truth(rendered, test_case, layout, options);
    return rendered;
}

RenderedCase render_bitmap(const SyntheticCase& test_case, const RenderOptions& options,
                           Color background, Color foreground) {
    const int scale = std::clamp(options.font_px / 8, 1, 6);
    const int advance = 8 * scale + scale;
    LineLayout layout;
    layout.text_height = 8 * scale;
    layout.line_height = layout.text_height + 6 * scale;
    layout.char_x.resize(test_case.lines.size());
    std::size_t max_chars = 1;
    for (std::size_t i = 0; i < test_case.lines.size(); ++i) {
        max_chars = std::max(max_chars, test_case.lines[i].size());
        for (std::size_t col = 0; col <= test_case.lines[i].size(); ++col) {
            layout.char_x[i].push_back(static_cast<float>(col) * static_cast<float>(advance));
        }
    }
    RenderedCase rendered;
    const int width = options.margin_px * 2 + static_cast<int>(max_chars) * advance;
    const int height =
        options.margin_px * 2 + static_cast<int>(test_case.lines.size()) * layout.line_height;
    rendered.image = secretsimage::Image(width, height, background);
    rendered.ground_truth.image_width = width;
    rendered.ground_truth.image_height = height;
    for (std::size_t line_index = 0; line_index < test_case.lines.size(); ++line_index) {
        const std::string& line = test_case.lines[line_index];
        for (std::size_t col = 0; col < line.size(); ++col) {
            const auto code = static_cast<unsigned char>(line[col]);
            if (code >= 128) continue;
            const int origin_x = options.margin_px + static_cast<int>(col) * advance;
            const int origin_y =
                options.margin_px + static_cast<int>(line_index) * layout.line_height;
            for (int row = 0; row < 8; ++row) {
                const unsigned char bits = static_cast<unsigned char>(font8x8_basic[code][row]);
                for (int bit = 0; bit < 8; ++bit) {
                    if (((bits >> bit) & 1U) == 0U) continue;
                    for (int dy = 0; dy < scale; ++dy) {
                        for (int dx = 0; dx < scale; ++dx) {
                            rendered.image.set_pixel(origin_x + bit * scale + dx,
                                                     origin_y + row * scale + dy, foreground);
                        }
                    }
                }
            }
        }
    }
    // The bitmap word boxes use the same char geometry as the TTF path.
    add_ground_truth(rendered, test_case, layout, options);
    return rendered;
}

}  // namespace

RenderedCase render_case(const SyntheticCase& test_case, const RenderOptions& options) {
    const Color background =
        options.dark_theme ? Color{24, 24, 24, 255} : Color{255, 255, 255, 255};
    const Color foreground =
        options.dark_theme ? Color{235, 235, 235, 255} : Color{16, 16, 16, 255};
    const TrueTypeFont& font = load_font();
    if (font.loaded) return render_truetype(test_case, options, font, background, foreground);
    return render_bitmap(test_case, options, background, foreground);
}

}  // namespace synthetic
