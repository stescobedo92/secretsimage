#pragma once

// Minimal maintained test harness: no external dependency, CTest-friendly.
// Each test ends in CHECK assertions; a failed CHECK throws TestFailure and
// is reported with file and line. Tests that need an optional external tool
// throw SkipTest so CTest reports them through the runner exit summary.
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace testfw {

struct TestCase {
    std::string name;
    std::string tag;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> all;
    return all;
}

struct Registrar {
    Registrar(std::string name, std::string tag, std::function<void()> body) {
        registry().push_back(TestCase{std::move(name), std::move(tag), std::move(body)});
    }
};

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct SkipTest : std::exception {
    std::string reason;
    explicit SkipTest(std::string why) : reason(std::move(why)) {}
    [[nodiscard]] const char* what() const noexcept override { return reason.c_str(); }
};

[[noreturn]] inline void fail(const char* file, int line, const std::string& message) {
    std::ostringstream out;
    out << file << ':' << line << ": " << message;
    throw TestFailure(out.str());
}

}  // namespace testfw

#define SECRETSIMAGE_TEST_CONCAT_INNER(a, b) a##b
#define SECRETSIMAGE_TEST_CONCAT(a, b) SECRETSIMAGE_TEST_CONCAT_INNER(a, b)

#define TEST_CASE_TAG(test_name, test_tag)                                                     \
    static void SECRETSIMAGE_TEST_CONCAT(test_body_, __LINE__)();                              \
    static ::testfw::Registrar SECRETSIMAGE_TEST_CONCAT(test_reg_, __LINE__){                  \
        test_name, test_tag, &SECRETSIMAGE_TEST_CONCAT(test_body_, __LINE__)};                 \
    static void SECRETSIMAGE_TEST_CONCAT(test_body_, __LINE__)()

#define TEST_CASE(test_name) TEST_CASE_TAG(test_name, "")

#define CHECK(expr)                                                                            \
    do {                                                                                       \
        if (!(expr)) ::testfw::fail(__FILE__, __LINE__, "CHECK failed: " #expr);               \
    } while (0)

#define CHECK_MSG(expr, message)                                                               \
    do {                                                                                       \
        if (!(expr)) {                                                                         \
            std::ostringstream check_out;                                                      \
            check_out << "CHECK failed: " #expr << " -- " << message;                          \
            ::testfw::fail(__FILE__, __LINE__, check_out.str());                               \
        }                                                                                      \
    } while (0)
