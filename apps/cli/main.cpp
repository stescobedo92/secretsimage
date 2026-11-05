// secretsimage command line interface.
//
// Modes:
//   redact  OCR + detect + solid-fill one or more images (file / CI mode)
//   scan    detect only; exit code 2 when secrets are present (CI gate)
//   watch   clipboard interception: replaces every new clipboard image with
//           its redacted version before it can be pasted into any app
//
// Exit codes: 0 success/clean, 1 error, 2 secrets found (scan, or redact
// with --fail-on-secret). Secret values are never printed or logged.
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "secretsimage/clipboard.hpp"
#include "secretsimage/ocr.hpp"
#include "secretsimage/pipeline.hpp"
#include "secretsimage/version.hpp"

namespace {

namespace fs = std::filesystem;
using namespace secretsimage;  // NOLINT(google-build-using-namespace) -- single-file app

void print_usage(std::ostream& out) {
    out << "secretsimage " << kVersion
        << " -- automatic redaction of secrets in screenshots\n"
           "\n"
           "Usage:\n"
           "  secretsimage redact <input...> -o <output> [--report <path>] [--fail-on-secret]\n"
           "  secretsimage redact <input...> --output-dir <dir> [--fail-on-secret]\n"
           "  secretsimage scan <input...>\n"
           "  secretsimage watch [--audit-log <path>] [--poll-ms <ms>] [--timeout <seconds>]\n"
           "  secretsimage version\n"
           "\n"
           "Shared options:\n"
           "  --ip-mode <private|all|off>  which IPv4 addresses count as sensitive (default private)\n"
           "  --padding <px>               solid-fill margin around findings (default 4)\n"
           "  --no-high-entropy            disable the generic high-entropy token rule\n"
           "\n"
           "Requires Tesseract OCR on PATH (or set SECRETSIMAGE_TESSERACT to its path).\n"
           "Audit reports never contain secret values, only kind, box and length.\n";
}

struct ParsedArgs {
    std::vector<fs::path> inputs;
    fs::path output;
    fs::path output_dir;
    fs::path report;
    fs::path audit_log;
    bool fail_on_secret = false;
    int poll_ms = 200;
    int timeout_seconds = 0;
    PipelineOptions pipeline;
};

[[nodiscard]] int parse_int_arg(const std::string& value, const char* name, int& out) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(value, &used);
        if (used != value.size()) throw std::invalid_argument("trailing input");
        out = parsed;
        return 0;
    } catch (...) {
        std::cerr << "error: invalid value for " << name << ": " << value << '\n';
        return 1;
    }
}

[[nodiscard]] int parse_args(int argc, char** argv, int first, ParsedArgs& args) {
    for (int i = first; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string& value) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "error: missing value for " << arg << '\n';
                return false;
            }
            value = argv[++i];
            return true;
        };
        std::string value;
        if (arg == "-o" || arg == "--output") {
            if (!next(value)) return 1;
            args.output = value;
        } else if (arg == "--output-dir") {
            if (!next(value)) return 1;
            args.output_dir = value;
        } else if (arg == "--report") {
            if (!next(value)) return 1;
            args.report = value;
        } else if (arg == "--audit-log") {
            if (!next(value)) return 1;
            args.audit_log = value;
        } else if (arg == "--fail-on-secret") {
            args.fail_on_secret = true;
        } else if (arg == "--no-high-entropy") {
            args.pipeline.detector.detect_high_entropy = false;
        } else if (arg == "--ip-mode") {
            if (!next(value)) return 1;
            if (value == "private") {
                args.pipeline.detector.ip_mode = IpMode::kPrivateOnly;
            } else if (value == "all") {
                args.pipeline.detector.ip_mode = IpMode::kAll;
            } else if (value == "off") {
                args.pipeline.detector.ip_mode = IpMode::kOff;
            } else {
                std::cerr << "error: --ip-mode must be private, all or off\n";
                return 1;
            }
        } else if (arg == "--padding") {
            if (!next(value)) return 1;
            if (parse_int_arg(value, "--padding", args.pipeline.redaction.padding_px) != 0) return 1;
            if (args.pipeline.redaction.padding_px < 0) args.pipeline.redaction.padding_px = 0;
        } else if (arg == "--poll-ms") {
            if (!next(value)) return 1;
            if (parse_int_arg(value, "--poll-ms", args.poll_ms) != 0) return 1;
        } else if (arg == "--timeout") {
            if (!next(value)) return 1;
            if (parse_int_arg(value, "--timeout", args.timeout_seconds) != 0) return 1;
        } else if (arg == "--ocr-upscale") {
            if (!next(value)) return 1;
            if (parse_int_arg(value, "--ocr-upscale", args.pipeline.ocr.upscale_factor) != 0) return 1;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown option: " << arg << '\n';
            return 1;
        } else {
            args.inputs.push_back(arg);
        }
    }
    return 0;
}

void print_findings(std::ostream& out, const fs::path& file, const PipelineResult& result) {
    out << file.filename().string() << ": " << result.findings.size() << " finding(s) in "
        << result.elapsed_ms << " ms";
    for (const Finding& finding : result.findings) {
        out << "\n  " << to_string(finding.kind) << " at [" << finding.box.x << ',' << finding.box.y
            << ' ' << finding.box.width << 'x' << finding.box.height << "] len "
            << finding.matched_length;
    }
    out << '\n';
}

int run_redact(ParsedArgs& args) {
    if (args.inputs.empty()) {
        std::cerr << "error: redact needs at least one input image\n";
        return 1;
    }
    if (args.output.empty() && args.output_dir.empty()) {
        std::cerr << "error: redact needs -o <output> or --output-dir <dir>\n";
        return 1;
    }
    if (!args.output.empty() && args.inputs.size() > 1) {
        std::cerr << "error: use --output-dir for multiple inputs\n";
        return 1;
    }
    auto ocr = make_default_ocr_engine();
    bool any_secrets = false;
    for (const fs::path& input : args.inputs) {
        const fs::path output = !args.output_dir.empty()
                                      ? args.output_dir / (input.stem().string() + ".redacted" +
                                                           (input.extension().empty()
                                                                ? std::string(".png")
                                                                : input.extension().string()))
                                      : args.output;
        fs::path report = args.report;
        if (report.empty()) report = output.string() + ".audit.json";
        auto result = redact_file(input, output, report, *ocr, args.pipeline);
        if (!result) {
            std::cerr << "error: " << input.string() << ": " << result.error().message << '\n';
            return 1;
        }
        any_secrets = any_secrets || result->has_secrets();
        print_findings(std::cout, input, *result);
        std::cout << "  redacted -> " << output.string() << "\n  report   -> " << report.string()
                  << '\n';
    }
    return args.fail_on_secret && any_secrets ? 2 : 0;
}

int run_scan(ParsedArgs& args) {
    if (args.inputs.empty()) {
        std::cerr << "error: scan needs at least one input image\n";
        return 1;
    }
    auto ocr = make_default_ocr_engine();
    bool any_secrets = false;
    for (const fs::path& input : args.inputs) {
        auto image = Image::load_file(input);
        if (!image) {
            std::cerr << "error: " << input.string() << ": " << image.error().message << '\n';
            return 1;
        }
        auto findings = detect_with_ocr(*image, *ocr, args.pipeline);
        if (!findings) {
            std::cerr << "error: " << input.string() << ": " << findings.error().message << '\n';
            return 1;
        }
        PipelineResult result;
        result.findings = std::move(*findings);
        any_secrets = any_secrets || result.has_secrets();
        print_findings(std::cout, input, result);
    }
    return any_secrets ? 2 : 0;
}

ClipboardWatcher* g_watcher = nullptr;

void handle_stop_signal(int /*signal*/) {
    if (g_watcher != nullptr) g_watcher->stop();
}

int run_watch(ParsedArgs& args) {
    auto clipboard = make_platform_clipboard();
    if (!clipboard) {
        std::cerr << "error: " << clipboard.error().message << '\n';
        return 1;
    }
    auto ocr = make_default_ocr_engine();
    WatchOptions options;
    options.pipeline = args.pipeline;
    options.audit_log_path = args.audit_log;
    options.poll_interval_ms = args.poll_ms;
    options.timeout_seconds = args.timeout_seconds;
    options.on_event = [](const std::string& message) {
        std::cout << message << '\n';
        std::cout.flush();
        return true;
    };
    ClipboardWatcher watcher(std::move(*clipboard), *ocr, std::move(options));
    g_watcher = &watcher;
    std::signal(SIGINT, &handle_stop_signal);
    std::signal(SIGTERM, &handle_stop_signal);
    std::cout << "secretsimage " << kVersion
              << " watching the clipboard. Copy or screenshot an image and it is redacted before "
                 "you paste it into any application. Ctrl+C stops.\n";
    std::cout.flush();
    auto ran = watcher.run();
    g_watcher = nullptr;
    if (!ran) {
        std::cerr << "error: " << ran.error().message << '\n';
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(std::cerr);
        return 1;
    }
    const std::string command = argv[1];
    if (command == "help" || command == "--help" || command == "-h") {
        print_usage(std::cout);
        return 0;
    }
    if (command == "version" || command == "--version") {
        std::cout << "secretsimage " << secretsimage::kVersion << '\n';
        return 0;
    }
    ParsedArgs args;
    if (parse_args(argc, argv, 2, args) != 0) return 1;
    try {
        if (command == "redact") return run_redact(args);
        if (command == "scan") return run_scan(args);
        if (command == "watch") return run_watch(args);
    } catch (const std::exception& error) {
        std::cerr << "error: unexpected failure: " << error.what() << '\n';
        return 1;
    }
    std::cerr << "error: unknown command: " << command << "\n\n";
    print_usage(std::cerr);
    return 1;
}
