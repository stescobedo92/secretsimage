#include "secretsimage/ocr.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace secretsimage {
namespace {

struct TempFiles {
    std::filesystem::path image_path;
    std::filesystem::path output_base;
    ~TempFiles() {
        std::error_code ec;
        std::filesystem::remove(image_path, ec);
        std::filesystem::remove(output_base.string() + ".tsv", ec);
    }
};

[[nodiscard]] std::filesystem::path unique_temp_base() {
    static std::atomic<unsigned long long> counter{0};
    const auto stamp = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#if defined(_WIN32)
    const auto process_id = static_cast<unsigned long long>(_getpid());
#else
    const auto process_id = static_cast<unsigned long long>(getpid());
#endif
    std::ostringstream name;
    name << "secretsimage-ocr-" << process_id << '-' << stamp << '-'
         << counter.fetch_add(1, std::memory_order_relaxed);
    return std::filesystem::temp_directory_path() / name.str();
}

// Runs a fixed command line built only from our own executable path, our own
// temporary file names and integer/whitelisted options. Returns the exit
// code, or -1 when the process could not be started.
[[nodiscard]] int run_capture(const std::string& command, std::string& captured) {
#if defined(_WIN32)
    FILE* pipe = _popen(command.c_str(), "rb");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (pipe == nullptr) return -1;
    std::array<char, 4096> buffer{};
    while (std::size_t read = std::fread(buffer.data(), 1, buffer.size(), pipe)) {
        captured.append(buffer.data(), read);
        if (captured.size() > (1U << 20U)) break;  // version banners are tiny
    }
#if defined(_WIN32)
    return _pclose(pipe);
#else
    const int status = pclose(pipe);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

[[nodiscard]] bool quote_safe(const std::string& text) noexcept {
    // Paths or options containing shell metacharacters are rejected rather
    // than escaped: this program generates its own temp paths, and the
    // tesseract executable path comes from local configuration only.
    return text.find_first_of("\"'`$&;|<>!\n\r") == std::string::npos;
}

[[nodiscard]] std::string quoted(const std::filesystem::path& path) {
    return '"' + path.string() + '"';
}

[[nodiscard]] std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == '\t') {
            fields.push_back(line.substr(begin, i - begin));
            begin = i + 1;
        }
    }
    return fields;
}

[[nodiscard]] int parse_int(const std::string& text) noexcept {
    return std::atoi(text.c_str());  // TSV numbers are engine-generated; junk maps to 0
}

[[nodiscard]] float parse_float(const std::string& text) noexcept {
    return static_cast<float>(std::atof(text.c_str()));
}

// Nearest-neighbor upscale. Screenshots are synthetic, sharply rendered
// content: nearest neighbor keeps glyph edges crisp instead of blurring
// them, which is what the OCR binarizer wants.
[[nodiscard]] Image upscale_nearest(const Image& image, int factor) {
    Image scaled(image.width() * factor, image.height() * factor);
    const auto source = image.pixels();
    auto target = scaled.pixels();
    const std::size_t source_stride = static_cast<std::size_t>(image.width()) * 4U;
    const std::size_t target_stride = static_cast<std::size_t>(scaled.width()) * 4U;
    for (int y = 0; y < image.height(); ++y) {
        const std::uint8_t* source_row = source.data() + static_cast<std::size_t>(y) * source_stride;
        for (int band = 0; band < factor; ++band) {
            std::uint8_t* target_row =
                target.data() + static_cast<std::size_t>(y * factor + band) * target_stride;
            for (int x = 0; x < image.width(); ++x) {
                const std::uint8_t* pixel = source_row + static_cast<std::size_t>(x) * 4U;
                for (int dx = 0; dx < factor; ++dx) {
                    std::uint8_t* out =
                        target_row + static_cast<std::size_t>(x * factor + dx) * 4U;
                    out[0] = pixel[0];
                    out[1] = pixel[1];
                    out[2] = pixel[2];
                    out[3] = pixel[3];
                }
            }
        }
    }
    return scaled;
}

[[nodiscard]] int clamp_upscale_factor(const Image& image, int requested) noexcept {
    int factor = std::clamp(requested, 1, 4);
    while (factor > 1) {
        const auto pixels = static_cast<std::int64_t>(image.width()) * factor *
                            static_cast<std::int64_t>(image.height()) * factor;
        if (pixels <= 80'000'000) break;  // keep temp images and OCR time bounded
        --factor;
    }
    return factor;
}

}  // namespace

TesseractCliOcrEngine::TesseractCliOcrEngine(std::filesystem::path executable)
    : executable_(std::move(executable)) {}

bool TesseractCliOcrEngine::is_available() {
    if (available_cached_ >= 0) return available_cached_ == 1;
    std::string out;
    const std::string exe = executable_.string();
#if defined(_WIN32)
    const std::string sink = " 2>NUL";
#else
    const std::string sink = " 2>/dev/null";
#endif
    const int code =
        quote_safe(exe) ? run_capture(quoted(executable_) + " --version" + sink, out) : -1;
    available_cached_ = code == 0 ? 1 : 0;
    return available_cached_ == 1;
}

Result<OcrResult> TesseractCliOcrEngine::recognize(const Image& image, const OcrOptions& options) {
    if (image.empty()) {
        return failure(ErrorCode::kInvalidArgument, "OCR requires a non-empty image");
    }
    if (!is_available()) {
        return failure(ErrorCode::kOcrUnavailable,
                       "tesseract executable was not found or did not run: " +
                           executable_.string() +
                           " (install Tesseract OCR and ensure it is on PATH)");
    }
    if (!quote_safe(options.language) || options.language.size() > 32) {
        return failure(ErrorCode::kInvalidArgument, "invalid OCR language option");
    }

    const int factor = clamp_upscale_factor(image, options.upscale_factor);
    const Image scaled = factor > 1 ? upscale_nearest(image, factor) : image;

    TempFiles temp;
    temp.output_base = unique_temp_base();
    temp.image_path = temp.output_base.string() + ".png";
    if (auto saved = scaled.save_file(temp.image_path); !saved) {
        return std::unexpected(saved.error());
    }

    std::ostringstream command;
    command << quoted(executable_) << ' ' << quoted(temp.image_path) << ' '
            << quoted(temp.output_base) << " --psm "
            << std::clamp(options.page_segmentation_mode, 0, 13) << " -l " << options.language
            << " tsv 2>/dev/null";
#if defined(_WIN32)
    // NUL is the Windows null device; stderr is discarded to keep console
    // output clean while the exit code still reports failure.
    std::string cmd = command.str();
    const std::string posix_sink = "2>/dev/null";
    cmd.replace(cmd.size() - posix_sink.size(), posix_sink.size(), "2>NUL");
#else
    const std::string cmd = command.str();
#endif
    std::string captured;
    if (run_capture(cmd, captured) != 0) {
        return failure(ErrorCode::kOcrFailed, "tesseract exited with a non-zero status");
    }

    std::ifstream in(temp.output_base.string() + ".tsv", std::ios::binary);
    if (!in) {
        return failure(ErrorCode::kOcrFailed, "tesseract produced no TSV output");
    }
    std::ostringstream tsv;
    tsv << in.rdbuf();
    auto result = parse_tesseract_tsv(tsv.str(), image.width(), image.height());
    if (result && factor > 1) {
        for (OcrWord& word : result->words) {
            word.box.x /= factor;
            word.box.y /= factor;
            word.box.width = std::max(1, word.box.width / factor);
            word.box.height = std::max(1, word.box.height / factor);
        }
    }
    return result;
}

Result<OcrResult> parse_tesseract_tsv(const std::string& tsv, int image_width, int image_height) {
    OcrResult result;
    result.image_width = image_width;
    result.image_height = image_height;
    std::istringstream input(tsv);
    std::string line;
    if (!std::getline(input, line)) {
        return failure(ErrorCode::kOcrFailed, "empty TSV output from OCR engine");
    }
    const std::vector<std::string> header = split_tabs(line);
    auto column = [&header](const char* name) -> int {
        for (std::size_t i = 0; i < header.size(); ++i) {
            std::string field = header[i];
            if (!field.empty() && field.back() == '\r') field.pop_back();
            if (field == name) return static_cast<int>(i);
        }
        return -1;
    };
    const int c_level = column("level");
    const int c_block = column("block_num");
    const int c_par = column("par_num");
    const int c_line = column("line_num");
    const int c_left = column("left");
    const int c_top = column("top");
    const int c_width = column("width");
    const int c_height = column("height");
    const int c_conf = column("conf");
    const int c_text = column("text");
    if (c_level < 0 || c_left < 0 || c_top < 0 || c_width < 0 || c_height < 0 || c_text < 0) {
        return failure(ErrorCode::kOcrFailed, "unexpected TSV header from OCR engine");
    }
    auto field = [](const std::vector<std::string>& fields, int index) -> std::string {
        if (index < 0 || static_cast<std::size_t>(index) >= fields.size()) return {};
        std::string value = fields[static_cast<std::size_t>(index)];
        if (!value.empty() && value.back() == '\r') value.pop_back();
        return value;
    };
    while (std::getline(input, line)) {
        const std::vector<std::string> fields = split_tabs(line);
        if (parse_int(field(fields, c_level)) != 5) continue;  // 5 = word level
        std::string text = field(fields, c_text);
        if (text.empty()) continue;
        OcrWord word;
        word.text = std::move(text);
        word.box = Box{parse_int(field(fields, c_left)), parse_int(field(fields, c_top)),
                       parse_int(field(fields, c_width)), parse_int(field(fields, c_height))};
        word.confidence = c_conf >= 0 ? parse_float(field(fields, c_conf)) : -1.0F;
        word.block = parse_int(field(fields, c_block));
        word.paragraph = parse_int(field(fields, c_par));
        word.line = parse_int(field(fields, c_line));
        if (!word.box.empty()) result.words.push_back(std::move(word));
    }
    return result;
}

std::unique_ptr<IOcrEngine> make_default_ocr_engine() {
    const char* from_env = std::getenv("SECRETSIMAGE_TESSERACT");
    if (from_env != nullptr && from_env[0] != '\0') {
        return std::make_unique<TesseractCliOcrEngine>(std::filesystem::path(from_env));
    }
    return std::make_unique<TesseractCliOcrEngine>();
}

}  // namespace secretsimage
