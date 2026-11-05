#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "secretsimage/image.hpp"
#include "secretsimage/ocr.hpp"
#include "secretsimage/types.hpp"

// Deterministic synthetic screenshots for the reproducible test plans
// described in docs/TEST_PLAN.md (human review PR-3 and automated PR-4).
// Every secret is fake but format-real: checksums (Luhn, IBAN mod-97) are
// computed so validators see genuinely valid numbers.
namespace synthetic {

struct SecretSpan {
    std::size_t line = 0;
    std::size_t begin = 0;  // character offsets inside the line
    std::size_t end = 0;
    secretsimage::SecretKind kind = secretsimage::SecretKind::kHighEntropyToken;
};

struct SyntheticCase {
    std::string name;
    std::vector<std::string> lines;
    std::vector<SecretSpan> secrets;
};

// The standard corpus: 60 cases containing secrets + 20 clean cases
// (parameterizable for quicker local runs). Same seed => same corpus.
[[nodiscard]] std::vector<SyntheticCase> make_corpus(std::uint64_t seed = 20261105ULL,
                                                     int secret_cases = 60, int clean_cases = 20);

struct RenderOptions {
    // Font size in pixels (the PR-3/PR-4 matrix varies 10..18 px). Text is
    // rendered from the vendored DejaVu Sans Mono TrueType font so OCR sees
    // realistic glyphs; a public-domain 8x8 bitmap font is the fallback.
    int font_px = 18;
    bool dark_theme = false;
    int margin_px = 16;
};

struct RenderedCase {
    secretsimage::Image image;
    secretsimage::OcrResult ground_truth;  // perfect OCR of the rendered text
    std::vector<secretsimage::Box> secret_boxes;  // one per SyntheticCase::secrets entry
    std::vector<bool> word_is_secret;             // parallel to ground_truth.words
};

[[nodiscard]] RenderedCase render_case(const SyntheticCase& test_case,
                                       const RenderOptions& options = {});

}  // namespace synthetic
