// Regression tests for Tesseract discovery: the standard Windows installer
// (UB Mannheim) and Homebrew put `tesseract` in a well-known directory that
// is frequently not on PATH, so the default engine must probe those
// locations. These tests are hermetic: they build fake install trees under
// a temp directory and never invoke a real OCR engine.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

#include "secretsimage/ocr.hpp"
#include "test_framework.hpp"

namespace {
using namespace secretsimage;  // NOLINT -- test translation unit
namespace fs = std::filesystem;

[[nodiscard]] fs::path standard_candidate(const fs::path& root) {
#if defined(_WIN32)
    return root / "Tesseract-OCR" / "tesseract.exe";
#else
    return root / "bin" / "tesseract";
#endif
}

struct TempTree {
    fs::path base;
    explicit TempTree(const char* label) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        base = fs::temp_directory_path() /
               ("secretsimage-discovery-" + std::string(label) + "-" + std::to_string(stamp));
        fs::create_directories(base);
    }
    ~TempTree() {
        std::error_code ec;
        fs::remove_all(base, ec);
    }
    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;
    TempTree(TempTree&&) = delete;
    TempTree& operator=(TempTree&&) = delete;
};

void install_fake_tesseract(const fs::path& root) {
    const fs::path candidate = standard_candidate(root);
    fs::create_directories(candidate.parent_path());
    std::ofstream(candidate) << "fake\n";
}

}  // namespace

TEST_CASE("ocr discovery: finds a standard install under a later root") {
    TempTree tree("later");
    const fs::path empty_root = tree.base / "empty";
    const fs::path installed_root = tree.base / "installed";
    fs::create_directories(empty_root);
    install_fake_tesseract(installed_root);
    CHECK(find_standard_tesseract({empty_root, installed_root}) ==
          standard_candidate(installed_root));
}

TEST_CASE("ocr discovery: the first matching root wins") {
    TempTree tree("first");
    const fs::path first = tree.base / "first";
    const fs::path second = tree.base / "second";
    install_fake_tesseract(first);
    install_fake_tesseract(second);
    CHECK(find_standard_tesseract({first, second}) == standard_candidate(first));
}

TEST_CASE("ocr discovery: no install yields an empty path") {
    TempTree tree("none");
    const fs::path root = tree.base / "root";
    fs::create_directories(root);
    CHECK(find_standard_tesseract({root}).empty());
    CHECK(find_standard_tesseract({}).empty());
    // A directory named like the executable is not an executable.
    fs::create_directories(standard_candidate(root));
    CHECK(find_standard_tesseract({root}).empty());
}

TEST_CASE("ocr discovery: an explicit bad executable stays unavailable") {
    TesseractCliOcrEngine engine("secretsimage-no-such-tesseract-binary");
    CHECK(!engine.is_available());
    auto result = engine.recognize(Image(2, 2), {});
    CHECK(!result.has_value());
    CHECK(result.error().code == ErrorCode::kOcrUnavailable);
    CHECK_MSG(result.error().message.find("SECRETSIMAGE_TESSERACT") != std::string::npos,
              "error must name the override env var: " + result.error().message);
}
