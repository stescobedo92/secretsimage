#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "secretsimage/error.hpp"
#include "secretsimage/image.hpp"
#include "secretsimage/types.hpp"

namespace secretsimage {

// One recognized word with its bounding box in image pixels. block/par/line
// identify the reading-order line the word belongs to; the detector joins
// words of one line before pattern matching so secrets split by spaces
// (connection strings, labelled assignments) are still found.
struct OcrWord {
    std::string text;
    Box box;
    float confidence = 0.0F;  // 0..100 as reported by the engine, -1 if unknown
    int block = 0;
    int paragraph = 0;
    int line = 0;
};

struct OcrResult {
    std::vector<OcrWord> words;
    int image_width = 0;
    int image_height = 0;
};

struct OcrOptions {
    // Tesseract page segmentation mode. 6 = uniform block of text, which
    // matches terminals and document screenshots well.
    int page_segmentation_mode = 6;
    std::string language = "eng";
    // Screenshots carry 10-18 px text, well below the ~30 px x-height OCR
    // engines read best. The image is upscaled by this factor before OCR
    // and word boxes are scaled back. 1 disables upscaling.
    int upscale_factor = 2;
};

class IOcrEngine {
  public:
    virtual ~IOcrEngine() = default;
    IOcrEngine(const IOcrEngine&) = delete;
    IOcrEngine& operator=(const IOcrEngine&) = delete;
    IOcrEngine(IOcrEngine&&) = delete;
    IOcrEngine& operator=(IOcrEngine&&) = delete;

    [[nodiscard]] virtual Result<OcrResult> recognize(const Image& image,
                                                      const OcrOptions& options) = 0;
    [[nodiscard]] virtual std::string name() const = 0;
    // Whether the engine can run at all (e.g. its external program exists).
    // Long-running callers check this once at startup and fail fast instead
    // of reporting the same error for every image.
    [[nodiscard]] virtual bool is_available() { return true; }

  protected:
    IOcrEngine() = default;
};

// OCR through the `tesseract` command line program using TSV output. This
// needs no linked dependency, works identically on Windows, Linux and
// macOS, and isolates native OCR crashes from the host process. The engine
// only ever invokes a fixed argument vector on a temporary file it created
// itself, so no user input reaches a shell.
//
// Executable resolution for the default `"tesseract"` name, in order: the
// `SECRETSIMAGE_TESSERACT` environment override, `PATH`, then the standard
// install locations of the platform installers (on Windows the UB Mannheim
// installer does not add itself to `PATH` unless asked). An explicitly
// passed executable is used as-is with no fallback.
class TesseractCliOcrEngine final : public IOcrEngine {
  public:
    explicit TesseractCliOcrEngine(std::filesystem::path executable = "tesseract");

    [[nodiscard]] Result<OcrResult> recognize(const Image& image,
                                              const OcrOptions& options) override;
    [[nodiscard]] std::string name() const override { return "tesseract-cli"; }

    // Checks the executable responds to `--version`. The result is cached.
    [[nodiscard]] bool is_available() override;

  private:
    std::filesystem::path executable_;
    int available_cached_ = -1;  // -1 unknown, 0 no, 1 yes
};

[[nodiscard]] std::unique_ptr<IOcrEngine> make_default_ocr_engine();

// First standard-install Tesseract executable found under `roots`, or an
// empty path. Each root is an install prefix, probed as
// `<root>/Tesseract-OCR/tesseract.exe` on Windows and `<root>/bin/tesseract`
// elsewhere. Exposed for unit testing.
[[nodiscard]] std::filesystem::path find_standard_tesseract(
    const std::vector<std::filesystem::path>& roots);

// Parses Tesseract TSV output into words. Exposed for unit testing.
[[nodiscard]] Result<OcrResult> parse_tesseract_tsv(const std::string& tsv, int image_width,
                                                    int image_height);

}  // namespace secretsimage
