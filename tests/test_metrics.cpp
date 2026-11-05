// Automated counterpart of the reproducible test plan (docs/TEST_PLAN.md).
// Runs the full corpus (60 secret + 20 clean synthetic screenshots) through
// detection + solid-fill using ground-truth OCR words, and enforces the
// agreed metrics:
//   - recall of tokens/keys >= 99% (whole-corpus recall is reported too)
//   - residual leak: every secret box is 100% solid fill after redaction
//   - over-redaction of clean words <= 5%
//   - p95 latency per image <= 2 s (pipeline time, OCR excluded here; the
//     OCR-inclusive budget is exercised by the ocr_e2e test)
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

#include "secretsimage/pipeline.hpp"
#include "synthetic.hpp"
#include "test_framework.hpp"

namespace {

using namespace secretsimage;  // NOLINT -- test translation unit

struct Metrics {
    std::size_t secret_total = 0;
    std::size_t secret_covered = 0;
    std::size_t token_secret_total = 0;
    std::size_t token_secret_covered = 0;
    std::size_t clean_word_total = 0;
    std::size_t clean_word_painted = 0;
    std::vector<double> latencies_ms;
};

[[nodiscard]] bool is_token_kind(SecretKind kind) noexcept {
    switch (kind) {
        case SecretKind::kPrivateKey:
        case SecretKind::kAwsAccessKey:
        case SecretKind::kGcpApiKey:
        case SecretKind::kGitHubToken:
        case SecretKind::kSlackToken:
        case SecretKind::kJwt:
        case SecretKind::kBearerToken:
        case SecretKind::kConnectionString:
        case SecretKind::kLabelledSecret:
            return true;
        default:
            return false;
    }
}

void evaluate_rendering(const synthetic::RenderOptions& render, Metrics& metrics) {
    const auto corpus = synthetic::make_corpus();
    for (const synthetic::SyntheticCase& test_case : corpus) {
        synthetic::RenderedCase rendered = synthetic::render_case(test_case, render);
        const auto start = std::chrono::steady_clock::now();
        const PipelineResult result =
            redact_with_words(rendered.image, rendered.ground_truth, {});
        const auto elapsed = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
        metrics.latencies_ms.push_back(elapsed);

        for (std::size_t i = 0; i < rendered.secret_boxes.size(); ++i) {
            ++metrics.secret_total;
            const bool token_kind = is_token_kind(test_case.secrets[i].kind);
            if (token_kind) ++metrics.token_secret_total;
            // Covered means: not a single pixel of the secret remains visible.
            if (rendered.image.is_solid(rendered.secret_boxes[i], Color{0, 0, 0, 255})) {
                ++metrics.secret_covered;
                if (token_kind) ++metrics.token_secret_covered;
            }
        }
        for (const Box& box : result.painted_boxes) {
            CHECK_MSG(rendered.image.is_solid(box, Color{0, 0, 0, 255}),
                      "every painted region must be exactly one solid color");
        }
        for (std::size_t i = 0; i < rendered.ground_truth.words.size(); ++i) {
            if (rendered.word_is_secret[i]) continue;
            ++metrics.clean_word_total;
            const Box& word_box = rendered.ground_truth.words[i].box;
            const int center_x = word_box.x + word_box.width / 2;
            const int center_y = word_box.y + word_box.height / 2;
            for (const Box& painted : result.painted_boxes) {
                if (center_x >= painted.x && center_x < painted.right() && center_y >= painted.y &&
                    center_y < painted.bottom()) {
                    ++metrics.clean_word_painted;
                    break;
                }
            }
        }
    }
}

[[nodiscard]] double percentile95(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(0.95 * static_cast<double>(values.size() - 1));
    return values[index];
}

}  // namespace

TEST_CASE("metrics: recall, residual leak, over-redaction and latency meet targets") {
    Metrics metrics;
    // PR-4 matrix at ground-truth OCR level: light/dark themes, large and
    // small ends of the 10..18 px text range.
    evaluate_rendering(synthetic::RenderOptions{18, false, 16}, metrics);
    evaluate_rendering(synthetic::RenderOptions{12, true, 16}, metrics);

    const double recall = metrics.secret_total == 0
                              ? 1.0
                              : static_cast<double>(metrics.secret_covered) /
                                    static_cast<double>(metrics.secret_total);
    const double token_recall =
        metrics.token_secret_total == 0
            ? 1.0
            : static_cast<double>(metrics.token_secret_covered) /
                  static_cast<double>(metrics.token_secret_total);
    const double over_redaction =
        metrics.clean_word_total == 0
            ? 0.0
            : static_cast<double>(metrics.clean_word_painted) /
                  static_cast<double>(metrics.clean_word_total);
    const double p95 = percentile95(metrics.latencies_ms);

    std::cout << "[metrics] secrets " << metrics.secret_covered << '/' << metrics.secret_total
              << " recall=" << recall << " token_recall=" << token_recall << " (" << metrics.token_secret_covered
              << '/' << metrics.token_secret_total << ")"
              << " over_redaction=" << over_redaction << " (" << metrics.clean_word_painted << '/'
              << metrics.clean_word_total << ") p95_ms=" << p95 << '\n';

    CHECK_MSG(metrics.secret_covered == metrics.secret_total,
              "residual leak must be 0: every secret box fully solid-filled");
    CHECK_MSG(token_recall >= 0.99, "token/key recall target is >= 99%");
    CHECK_MSG(over_redaction <= 0.05, "over-redaction target is <= 5% of clean words");
    CHECK_MSG(p95 <= 2000.0, "p95 latency target is <= 2 s per image");
}

TEST_CASE("metrics: JPEG q60-q95 roundtrip keeps coordinates stable") {
    // Ground-truth words stay valid after lossy compression because geometry
    // does not change; this guards the encode/decode leg of the PR-4 matrix
    // in environments without a real OCR engine.
    const auto corpus = synthetic::make_corpus(7ULL, 4, 1);
    for (int quality : {60, 80, 95}) {
        for (const synthetic::SyntheticCase& test_case : corpus) {
            synthetic::RenderedCase rendered = synthetic::render_case(test_case, {});
            auto encoded = rendered.image.encode(ImageFormat::kJpeg, quality);
            CHECK(encoded.has_value());
            auto decoded = Image::decode(*encoded);
            CHECK(decoded.has_value());
            const PipelineResult result = redact_with_words(*decoded, rendered.ground_truth, {});
            for (const Box& secret_box : rendered.secret_boxes) {
                CHECK_MSG(decoded->is_solid(secret_box, Color{0, 0, 0, 255}),
                          "secret fully covered after JPEG roundtrip");
            }
            static_cast<void>(result);
        }
    }
}
