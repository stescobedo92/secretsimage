#include <filesystem>
#include <fstream>
#include <sstream>

#include "secretsimage/pipeline.hpp"
#include "secretsimage/sha256.hpp"
#include "synthetic.hpp"
#include "test_framework.hpp"

namespace {

using namespace secretsimage;  // NOLINT -- test translation unit
namespace fs = std::filesystem;

class FakeOcr final : public IOcrEngine {
  public:
    explicit FakeOcr(OcrResult preset) : preset_(std::move(preset)) {}
    Result<OcrResult> recognize(const Image&, const OcrOptions&) override { return preset_; }
    [[nodiscard]] std::string name() const override { return "fake"; }

  private:
    OcrResult preset_;
};

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

fs::path temp_dir() {
    const fs::path dir = fs::temp_directory_path() / "secretsimage-tests";
    fs::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("tesseract TSV parser extracts words with boxes") {
    const std::string tsv =
        "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n"
        "1\t1\t0\t0\t0\t0\t0\t0\t800\t600\t-1\t\n"
        "5\t1\t1\t1\t1\t1\t10\t20\t90\t18\t96.5\tpassword\n"
        "5\t1\t1\t1\t1\t2\t110\t20\t60\t18\t90\t=\n"
        "5\t1\t1\t1\t2\t1\t10\t48\t50\t18\t88\tclean\n";
    auto parsed = parse_tesseract_tsv(tsv, 800, 600);
    CHECK(parsed.has_value());
    CHECK(parsed->words.size() == 3);
    CHECK(parsed->words[0].text == "password");
    CHECK(parsed->words[0].box == Box(10, 20, 90, 18));
    CHECK(parsed->words[2].line == 2);
    CHECK(!parse_tesseract_tsv("nonsense\n", 10, 10).has_value());
}

TEST_CASE("pipeline redacts through an OCR engine") {
    const synthetic::SyntheticCase test_case{"case",
                                             {"password = Sunflower#42 today", "all clean here"},
                                             {synthetic::SecretSpan{0, 11, 23, SecretKind::kLabelledSecret}}};
    synthetic::RenderedCase rendered = synthetic::render_case(test_case, {});
    FakeOcr ocr(rendered.ground_truth);
    auto result = redact_with_ocr(rendered.image, ocr, {});
    CHECK(result.has_value());
    CHECK(result->has_secrets());
    for (const Box& box : result->painted_boxes) {
        CHECK(rendered.image.is_solid(box, Color{0, 0, 0, 255}));
    }
    CHECK(rendered.image.is_solid(rendered.secret_boxes[0], Color{0, 0, 0, 255}));
}

TEST_CASE("redact_file writes output and audit report without leaking the secret") {
    const std::string secret_value = "ghp_1234567890abcdefghijklmnopqrstuvwxyz";
    const synthetic::SyntheticCase test_case{"filecase",
                                             {"token here " + secret_value},
                                             {synthetic::SecretSpan{0, 11, 11 + secret_value.size(),
                                                                    SecretKind::kGitHubToken}}};
    const synthetic::RenderedCase rendered = synthetic::render_case(test_case, {});
    const fs::path dir = temp_dir();
    const fs::path input = dir / "input.png";
    const fs::path output = dir / "output.redacted.png";
    const fs::path report = dir / "output.audit.json";
    CHECK(rendered.image.save_file(input).has_value());
    const std::string input_before = read_file(input);

    FakeOcr ocr(rendered.ground_truth);
    auto result = redact_file(input, output, report, ocr, {});
    CHECK(result.has_value());
    CHECK(result->has_secrets());
    CHECK(fs::exists(output));
    CHECK(fs::exists(report));
    CHECK(read_file(input) == input_before);  // the original is never modified

    const std::string report_text = read_file(report);
    CHECK(report_text.find("github_token") != std::string::npos);
    CHECK_MSG(report_text.find(secret_value) == std::string::npos,
              "audit report must never contain the secret value");
    CHECK_MSG(report_text.find("1234567890abcdefghij") == std::string::npos,
              "audit report must not contain secret fragments");

    auto output_image = Image::load_file(output);
    CHECK(output_image.has_value());
    CHECK(output_image->is_solid(rendered.secret_boxes[0], Color{0, 0, 0, 255}));
}

TEST_CASE("redact_file refuses to overwrite its input") {
    const fs::path input = temp_dir() / "same.png";
    const Image image(8, 8, Color{255, 255, 255, 255});
    CHECK(image.save_file(input).has_value());
    FakeOcr ocr(OcrResult{});
    auto result = redact_file(input, input, {}, ocr, {});
    CHECK(!result.has_value());
}

TEST_CASE("pipeline leaves clean images untouched") {
    const synthetic::SyntheticCase test_case{"clean", {"Build finished with 0 warnings"}, {}};
    synthetic::RenderedCase rendered = synthetic::render_case(test_case, {});
    const std::string before = sha256_hex(rendered.image.pixels());
    const PipelineResult result = redact_with_words(rendered.image, rendered.ground_truth, {});
    CHECK(!result.has_secrets());
    CHECK(sha256_hex(rendered.image.pixels()) == before);
}
