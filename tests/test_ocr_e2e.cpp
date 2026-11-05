// End-to-end OCR test (PR-4 with a real engine): render synthetic secrets
// in a real TrueType font at screenshot sizes, run Tesseract, redact, run
// Tesseract again on the redacted image and require that no secret is
// recoverable. Coverage of the ground-truth secret boxes is measured too.
// Skipped when Tesseract is absent so the unit suite stays hermetic; CI
// installs Tesseract explicitly.
#include <iostream>

#include "secretsimage/ocr.hpp"
#include "secretsimage/pipeline.hpp"
#include "synthetic.hpp"
#include "test_framework.hpp"

namespace {
using namespace secretsimage;  // NOLINT -- test translation unit
}

TEST_CASE_TAG("ocr e2e: no secret survives redaction under a second OCR pass", "ocr") {
    TesseractCliOcrEngine engine;
    if (!engine.is_available()) {
        throw testfw::SkipTest("tesseract executable not available");
    }
    std::size_t secret_total = 0;
    std::size_t secret_covered = 0;
    std::size_t residual_findings = 0;
    for (int font_px : {18, 14}) {
        const auto corpus = synthetic::make_corpus(99ULL, 16, 4);
        for (const synthetic::SyntheticCase& test_case : corpus) {
            synthetic::RenderedCase rendered =
                synthetic::render_case(test_case, synthetic::RenderOptions{font_px, false, 16});
            const Image original = rendered.image;
            const Color background = original.pixel_at(0, 0);
            auto result = redact_with_ocr(rendered.image, engine, {});
            CHECK(result.has_value());
            for (const Box& box : result->painted_boxes) {
                CHECK(rendered.image.is_solid(box, Color{0, 0, 0, 255}));
            }
            // Coverage counts surviving ink: a secret is covered when at
            // least 99% of its original non-background pixels are solid
            // fill afterwards (real OCR boxes are glyph-tight, so comparing
            // whole rectangles would undercount true coverage).
            for (std::size_t secret_index = 0; secret_index < rendered.secret_boxes.size();
                 ++secret_index) {
                ++secret_total;
                const Box& secret_box = rendered.secret_boxes[secret_index];
                std::size_t ink = 0;
                std::size_t ink_surviving = 0;
                for (int y = secret_box.y; y < secret_box.bottom(); ++y) {
                    for (int x = secret_box.x; x < secret_box.right(); ++x) {
                        if (original.pixel_at(x, y) == background) continue;
                        ++ink;
                        if (!(rendered.image.pixel_at(x, y) == Color{0, 0, 0, 255})) {
                            ++ink_surviving;
                        }
                    }
                }
                if (ink == 0 || ink_surviving * 100 <= ink) {
                    ++secret_covered;
                } else {
                    std::cout << "[ocr-e2e] uncovered " << test_case.name << " font_px=" << font_px
                              << " expected=" << to_string(test_case.secrets[secret_index].kind)
                              << " ink_surviving=" << ink_surviving << '/' << ink << " findings:";
                    for (const Finding& finding : result->findings) {
                        std::cout << ' ' << to_string(finding.kind) << '[' << finding.box.x << ','
                                  << finding.box.y << ' ' << finding.box.width << 'x'
                                  << finding.box.height << ']';
                    }
                    std::cout << '\n';
                }
            }
            auto second_ocr = engine.recognize(rendered.image, {});
            CHECK(second_ocr.has_value());
            residual_findings += detect_secrets(*second_ocr, {}).size();
        }
    }
    const double coverage = secret_total == 0 ? 1.0
                                              : static_cast<double>(secret_covered) /
                                                    static_cast<double>(secret_total);
    std::cout << "[ocr-e2e] ground-truth coverage=" << secret_covered << '/' << secret_total << " ("
              << coverage << ") residual=" << residual_findings << '\n';
    CHECK_MSG(residual_findings == 0, "residual leak after redaction must be 0");
    // Real OCR is not perfect; the target in docs/TEST_PLAN.md is >= 99% for
    // tokens/keys on real screenshots. This synthetic floor guards against
    // regressions of the OCR leg without pretending OCR is infallible.
    CHECK_MSG(coverage >= 0.90, "real-OCR ground-truth coverage regressed below 90%");
}
