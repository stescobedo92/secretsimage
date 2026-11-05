#include <iostream>
#include <string>

#include "test_framework.hpp"

int main(int argc, char** argv) {
    std::string include_tag;
    std::string exclude_tag;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--tag" && i + 1 < argc) {
            include_tag = argv[++i];
        } else if (arg == "--exclude-tag" && i + 1 < argc) {
            exclude_tag = argv[++i];
        } else {
            std::cerr << "unknown argument: " << arg << '\n';
            return 2;
        }
    }

    int passed = 0;
    int failed = 0;
    int skipped = 0;
    for (const testfw::TestCase& test : testfw::registry()) {
        if (!include_tag.empty() && test.tag != include_tag) continue;
        if (!exclude_tag.empty() && test.tag == exclude_tag) continue;
        try {
            test.body();
            std::cout << "[PASS] " << test.name << '\n';
            ++passed;
        } catch (const testfw::SkipTest& skip) {
            std::cout << "[SKIP] " << test.name << ": " << skip.what() << '\n';
            ++skipped;
        } catch (const std::exception& error) {
            std::cout << "[FAIL] " << test.name << ": " << error.what() << '\n';
            ++failed;
        }
    }
    std::cout << "passed " << passed << ", failed " << failed << ", skipped " << skipped << '\n';
    return failed == 0 ? 0 : 1;
}
