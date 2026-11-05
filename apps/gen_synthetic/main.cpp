// Generates the deterministic synthetic screenshot corpus used by the
// human review (PR-3) and the automated matrix (PR-4). All secrets are
// fake but format-real. See docs/TEST_PLAN.md for the review protocol.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "synthetic.hpp"

namespace {

std::string json_escape(std::string_view text) {
    std::string out;
    for (char raw : text) {
        const auto ch = static_cast<unsigned char>(raw);
        if (ch == '"') {
            out += "\\\"";
        } else if (ch == '\\') {
            out += "\\\\";
        } else if (ch < 0x20U) {
            out += ' ';
        } else {
            out += static_cast<char>(ch);
        }
    }
    return out;
}

struct Options {
    std::filesystem::path out_dir = "corpus";
    std::uint64_t seed = 20261105ULL;
    int secret_cases = 60;
    int clean_cases = 20;
    int font_px = 18;
    int jpeg_quality = 0;  // 0 = PNG, otherwise JPEG with this quality (60..95)
};

}  // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string& value) {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };
        std::string value;
        try {
            if (arg == "--out" && next(value)) {
                options.out_dir = value;
            } else if (arg == "--seed" && next(value)) {
                options.seed = std::stoull(value);
            } else if (arg == "--secret-cases" && next(value)) {
                options.secret_cases = std::stoi(value);
            } else if (arg == "--clean-cases" && next(value)) {
                options.clean_cases = std::stoi(value);
            } else if (arg == "--font-px" && next(value)) {
                options.font_px = std::stoi(value);
            } else if (arg == "--jpeg-quality" && next(value)) {
                options.jpeg_quality = std::stoi(value);
            } else {
                std::cerr << "usage: gen_synthetic [--out DIR] [--seed N] [--secret-cases N] "
                             "[--clean-cases N] [--font-px N] [--jpeg-quality Q]\n";
                return 1;
            }
        } catch (...) {
            std::cerr << "error: invalid numeric argument\n";
            return 1;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(options.out_dir, ec);
    if (ec) {
        std::cerr << "error: cannot create output directory: " << ec.message() << '\n';
        return 1;
    }

    const auto corpus = synthetic::make_corpus(options.seed, options.secret_cases, options.clean_cases);
    std::ostringstream manifest;
    manifest << "{\n  \"note\": \"Synthetic corpus. Every secret below is FAKE but format-real; "
                "generated for PR-3 human review and PR-4 automation.\",\n  \"seed\": "
             << options.seed << ",\n  \"cases\": [\n";
    bool first_case = true;
    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const synthetic::SyntheticCase& test_case = corpus[i];
        synthetic::RenderOptions render;
        render.font_px = options.font_px;
        render.dark_theme = (i % 2 == 1);
        const synthetic::RenderedCase rendered = synthetic::render_case(test_case, render);
        const std::string ext = options.jpeg_quality > 0 ? ".jpg" : ".png";
        const std::filesystem::path file = options.out_dir / (test_case.name + ext);
        const auto saved = rendered.image.save_file(file, options.jpeg_quality > 0
                                                              ? options.jpeg_quality
                                                              : 90);
        if (!saved) {
            std::cerr << "error: " << saved.error().message << '\n';
            return 1;
        }
        if (!first_case) manifest << ",\n";
        first_case = false;
        manifest << "    {\"name\": \"" << json_escape(test_case.name) << "\", \"file\": \""
                 << json_escape(file.filename().string()) << "\", \"dark_theme\": "
                 << (render.dark_theme ? "true" : "false") << ", \"lines\": [";
        for (std::size_t j = 0; j < test_case.lines.size(); ++j) {
            if (j != 0) manifest << ", ";
            manifest << '"' << json_escape(test_case.lines[j]) << '"';
        }
        manifest << "], \"secrets\": [";
        for (std::size_t j = 0; j < test_case.secrets.size(); ++j) {
            if (j != 0) manifest << ", ";
            const auto& secret = test_case.secrets[j];
            const auto& box = rendered.secret_boxes[j];
            manifest << "{\"kind\": \"" << secretsimage::to_string(secret.kind)
                     << "\", \"line\": " << secret.line << ", \"box\": {\"x\": " << box.x
                     << ", \"y\": " << box.y << ", \"width\": " << box.width
                     << ", \"height\": " << box.height << "}}";
        }
        manifest << "]}";
    }
    manifest << "\n  ]\n}\n";

    const std::filesystem::path manifest_path = options.out_dir / "manifest.json";
    std::ofstream out(manifest_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "error: cannot write manifest\n";
        return 1;
    }
    out << manifest.str();
    std::cout << "wrote " << corpus.size() << " cases (" << options.secret_cases << " with secrets, "
              << options.clean_cases << " clean) to " << options.out_dir.string() << '\n';
    return 0;
}
