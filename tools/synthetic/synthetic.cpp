#include "synthetic.hpp"

#include <algorithm>
#include <random>
#include <sstream>

// Public-domain 8x8 bitmap font (see license note in the header itself).
// Defined as a mutable array upstream, so it is included in this single
// translation unit only. Its 0x80..0xFF bytes narrow into signed char,
// which is exactly the bit pattern we read back as unsigned.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
#pragma GCC diagnostic ignored "-Woverflow"
#endif
#include "font8x8_basic.h"  // NOLINT
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace synthetic {
namespace {

using secretsimage::Box;
using secretsimage::Color;
using secretsimage::SecretKind;

class Random {
  public:
    explicit Random(std::uint64_t seed) : engine_(seed) {}
    [[nodiscard]] int range(int low, int high) {
        return std::uniform_int_distribution<int>(low, high)(engine_);
    }
    [[nodiscard]] char pick(std::string_view alphabet) {
        return alphabet[static_cast<std::size_t>(range(0, static_cast<int>(alphabet.size()) - 1))];
    }
    [[nodiscard]] std::string token(std::string_view alphabet, std::size_t length) {
        std::string out;
        out.reserve(length);
        for (std::size_t i = 0; i < length; ++i) out += pick(alphabet);
        return out;
    }
    [[nodiscard]] std::string digits(std::size_t length) {
        return token("0123456789", length);
    }

  private:
    std::mt19937_64 engine_;
};

constexpr std::string_view kUpperDigits = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
constexpr std::string_view kB64Url =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
constexpr std::string_view kAlnum =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
constexpr std::string_view kHex = "0123456789abcdef";

[[nodiscard]] std::string make_card_number(Random& rng) {
    // Classic test PAN shape, finished with a correct Luhn check digit.
    std::string number = "4" + rng.digits(14);
    int sum = 0;
    bool double_digit = true;  // the check digit (not doubled) follows
    for (std::size_t i = number.size(); i-- > 0;) {
        int value = number[i] - '0';
        if (double_digit) {
            value *= 2;
            if (value > 9) value -= 9;
        }
        sum += value;
        double_digit = !double_digit;
    }
    number += static_cast<char>('0' + (10 - sum % 10) % 10);
    return number.substr(0, 4) + ' ' + number.substr(4, 4) + ' ' + number.substr(8, 4) + ' ' +
           number.substr(12, 4);
}

[[nodiscard]] std::string make_iban(Random& rng) {
    const std::string bban = rng.digits(20);  // ES BBAN: 20 digits
    auto mod97 = [](const std::string& numeric) {
        int remainder = 0;
        for (char ch : numeric) remainder = (remainder * 10 + (ch - '0')) % 97;
        return remainder;
    };
    // "ES" expands to 1428; check digits make IBAN mod-97 == 1.
    const int check = 98 - mod97(bban + "1428" + "00");
    std::string iban = "ES";
    if (check < 10) iban += '0';
    iban += std::to_string(check) + bban;
    std::string spaced;
    for (std::size_t i = 0; i < iban.size(); i += 4) {
        if (!spaced.empty()) spaced += ' ';
        spaced += iban.substr(i, 4);
    }
    return spaced;
}

struct SecretLine {
    std::string text;
    std::size_t begin = 0;
    std::size_t end = 0;
    SecretKind kind = SecretKind::kHighEntropyToken;
};

[[nodiscard]] SecretLine make_secret_line(Random& rng, int variant) {
    auto labelled = [](const std::string& label, const std::string& value, SecretKind kind) {
        return SecretLine{label + value, label.size(), label.size() + value.size(), kind};
    };
    switch (variant % 16) {
        case 0:
            return labelled("AWS_ACCESS_KEY_ID=", "AKIA" + rng.token(kUpperDigits, 16),
                            SecretKind::kAwsAccessKey);
        case 1: {
            const std::string value = "AIza" + rng.token(kB64Url, 35);
            return SecretLine{"api_key: " + value, 9, 9 + value.size(), SecretKind::kGcpApiKey};
        }
        case 2:
            return labelled("export GITHUB_TOKEN=", "ghp_" + rng.token(kAlnum, 36),
                            SecretKind::kGitHubToken);
        case 3: {
            const std::string value = "xoxb-" + rng.digits(10) + "-" + rng.token(kAlnum, 12);
            return SecretLine{"SLACK_TOKEN=" + value, 12, 12 + value.size(),
                              SecretKind::kSlackToken};
        }
        case 4: {
            const std::string jwt = "eyJ" + rng.token(kB64Url, 18) + "." +
                                    "eyJ" + rng.token(kB64Url, 24) + "." + rng.token(kB64Url, 32);
            return SecretLine{"Authorization: Bearer " + jwt, 22, 22 + jwt.size(), SecretKind::kJwt};
        }
        case 5: {
            const std::string value =
                "postgres://svc_app:" + rng.token(kAlnum, 14) + "@db.internal:5432/billing";
            return SecretLine{"DATABASE_URL=" + value, 13, 13 + value.size(),
                              SecretKind::kConnectionString};
        }
        case 6: {
            const std::string value = "Server=db01.corp.local;Database=erp;User Id=svc;Password=" +
                                      rng.token(kAlnum, 10) + "!";
            return SecretLine{value, 0, value.size(), SecretKind::kConnectionString};
        }
        case 7:
            return labelled("password = ", rng.token(kAlnum, 12) + "#7", SecretKind::kLabelledSecret);
        case 8: {
            const std::string mail = "maria.garcia" + rng.digits(2) + "@example-corp.com";
            return SecretLine{"Contact: " + mail + " (on call)", 9, 9 + mail.size(),
                              SecretKind::kEmail};
        }
        case 9: {
            const std::string ip = "10." + std::to_string(rng.range(0, 250)) + "." +
                                   std::to_string(rng.range(0, 250)) + "." +
                                   std::to_string(rng.range(2, 250));
            return SecretLine{"backend host " + ip + " unreachable", 13, 13 + ip.size(),
                              SecretKind::kPrivateIpv4};
        }
        case 10: {
            const std::string card = make_card_number(rng);
            return SecretLine{"card " + card + " declined", 5, 5 + card.size(),
                              SecretKind::kCreditCard};
        }
        case 11: {
            const std::string iban = make_iban(rng);
            return SecretLine{"pay to " + iban, 7, 7 + iban.size(), SecretKind::kIban};
        }
        case 12:
            return SecretLine{"-----BEGIN RSA PRIVATE KEY-----", 0, 31, SecretKind::kPrivateKey};
        case 13: {
            const std::string value = rng.token(kB64Url, 40);
            return SecretLine{"client_secret: " + value, 15, 15 + value.size(),
                              SecretKind::kLabelledSecret};
        }
        case 14: {
            const std::string ip = "2001:db8:85a3:" + rng.token(kHex, 4) + "::" + rng.token(kHex, 3);
            return SecretLine{"ipv6 peer " + ip + " dropped", 10, 10 + ip.size(), SecretKind::kIpv6};
        }
        default: {
            std::string serial = rng.digits(4);
            if (serial == "0000") serial = "0001";
            const std::string value = std::to_string(rng.range(100, 665)) + "-" +
                                      std::to_string(rng.range(10, 99)) + "-" + serial;
            return SecretLine{"ssn " + value + " on file", 4, 4 + value.size(), SecretKind::kSsn};
        }
    }
}

[[nodiscard]] std::string make_clean_line(Random& rng, int variant) {
    switch (variant % 10) {
        case 0: return "Build finished in 42 seconds with 0 warnings";
        case 1: return "commit " + rng.token(kHex, 40) + " merged";
        case 2: return "request id " + rng.token(kHex, 8) + "-" + rng.token(kHex, 4) + "-4" +
                       rng.token(kHex, 3) + "-a" + rng.token(kHex, 3) + "-" + rng.token(kHex, 12) +
                       " done";
        case 3: return "deploy at 12:30:45 UTC version 1.4.2 to production";
        case 4: return "public resolver 8.8.8.8 answered in 9 ms";
        case 5: return "Enter your password:";
        case 6: return "Total rows processed: 1234567 errors: 0";
        case 7: return "See the runbook page 12 section 3 for details";
        case 8: return "Order 1234 5678 9012 shipped on Tuesday";
        default: return "The quick brown fox jumps over the lazy dog";
    }
}

}  // namespace

std::vector<SyntheticCase> make_corpus(std::uint64_t seed, int secret_cases, int clean_cases) {
    Random rng(seed);
    std::vector<SyntheticCase> corpus;
    corpus.reserve(static_cast<std::size_t>(secret_cases + clean_cases));
    for (int i = 0; i < secret_cases; ++i) {
        SyntheticCase test_case;
        test_case.name = "secret-" + std::to_string(i);
        const int line_total = rng.range(3, 6);
        const int secret_line = rng.range(0, line_total - 1);
        for (int line = 0; line < line_total; ++line) {
            if (line == secret_line) {
                SecretLine secret = make_secret_line(rng, i);
                test_case.secrets.push_back(SecretSpan{static_cast<std::size_t>(line), secret.begin,
                                                       secret.end, secret.kind});
                test_case.lines.push_back(std::move(secret.text));
            } else {
                test_case.lines.push_back(make_clean_line(rng, rng.range(0, 9)));
            }
        }
        corpus.push_back(std::move(test_case));
    }
    for (int i = 0; i < clean_cases; ++i) {
        SyntheticCase test_case;
        test_case.name = "clean-" + std::to_string(i);
        const int line_total = rng.range(3, 6);
        for (int line = 0; line < line_total; ++line) {
            test_case.lines.push_back(make_clean_line(rng, rng.range(0, 9)));
        }
        corpus.push_back(std::move(test_case));
    }
    return corpus;
}

}  // namespace synthetic
