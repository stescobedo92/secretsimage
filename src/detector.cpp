#include "secretsimage/detector.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <regex>
#include <string>
#include <tuple>

namespace secretsimage {
namespace {

[[nodiscard]] bool is_digit(char ch) noexcept { return ch >= '0' && ch <= '9'; }

[[nodiscard]] std::array<int, 4> parse_ipv4(std::string_view text, bool& ok) noexcept {
    std::array<int, 4> octets{0, 0, 0, 0};
    ok = false;
    std::size_t part = 0;
    int value = 0;
    int digits = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char ch = i < text.size() ? text[i] : '.';
        if (ch == '.') {
            if (digits == 0 || digits > 3 || value > 255 || part >= 4) return octets;
            if (digits > 1 && text[i - static_cast<std::size_t>(digits)] == '0') return octets;
            octets[part++] = value;
            value = 0;
            digits = 0;
        } else if (is_digit(ch)) {
            value = value * 10 + (ch - '0');
            ++digits;
        } else {
            return octets;
        }
    }
    ok = part == 4;
    return octets;
}

}  // namespace

bool luhn_valid(std::string_view digits) noexcept {
    if (digits.size() < 13 || digits.size() > 19) return false;
    int sum = 0;
    bool double_digit = false;
    for (std::size_t i = digits.size(); i-- > 0;) {
        if (!is_digit(digits[i])) return false;
        int value = digits[i] - '0';
        if (double_digit) {
            value *= 2;
            if (value > 9) value -= 9;
        }
        sum += value;
        double_digit = !double_digit;
    }
    return sum % 10 == 0;
}

bool iban_valid(std::string_view candidate) noexcept {
    std::array<char, 34> compact{};
    std::size_t length = 0;
    for (char ch : candidate) {
        if (ch == ' ' || ch == '-') continue;
        if (length >= compact.size()) return false;
        compact[length++] = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    if (length < 15 || length > 34) return false;
    if (!std::isalpha(static_cast<unsigned char>(compact[0])) ||
        !std::isalpha(static_cast<unsigned char>(compact[1])) || !is_digit(compact[2]) ||
        !is_digit(compact[3])) {
        return false;
    }
    // ISO 13616: move the first 4 characters to the end, expand letters to
    // numbers (A=10..Z=35), the remainder mod 97 must be 1.
    int remainder = 0;
    auto feed = [&remainder](char ch) {
        if (ch >= '0' && ch <= '9') {
            remainder = (remainder * 10 + (ch - '0')) % 97;
        } else {
            const int value = ch - 'A' + 10;
            remainder = (remainder * 10 + value / 10) % 97;
            remainder = (remainder * 10 + value % 10) % 97;
        }
    };
    for (std::size_t i = 4; i < length; ++i) feed(compact[i]);
    for (std::size_t i = 0; i < 4; ++i) feed(compact[i]);
    return remainder == 1;
}

bool is_private_ipv4(std::string_view text) noexcept {
    bool ok = false;
    const auto o = parse_ipv4(text, ok);
    if (!ok) return false;
    if (o[0] == 10 || o[0] == 127) return true;                        // private + loopback
    if (o[0] == 172 && o[1] >= 16 && o[1] <= 31) return true;          // 172.16.0.0/12
    if (o[0] == 192 && o[1] == 168) return true;                       // 192.168.0.0/16
    if (o[0] == 169 && o[1] == 254) return true;                       // link-local
    if (o[0] == 100 && o[1] >= 64 && o[1] <= 127) return true;         // CGNAT 100.64.0.0/10
    return false;
}

double shannon_entropy_bits_per_char(std::string_view text) noexcept {
    if (text.empty()) return 0.0;
    std::array<std::size_t, 256> counts{};
    for (char raw : text) ++counts[static_cast<unsigned char>(raw)];
    double entropy = 0.0;
    const auto total = static_cast<double>(text.size());
    for (std::size_t count : counts) {
        if (count == 0) continue;
        const double p = static_cast<double>(count) / total;
        entropy -= p * (std::log(p) / std::log(2.0));
    }
    return entropy;
}

namespace {

struct LineWords {
    std::string text;
    // For each word: [char_begin, char_end) inside text and its box.
    std::vector<std::tuple<std::size_t, std::size_t, Box>> spans;
};

[[nodiscard]] std::vector<LineWords> build_lines(const OcrResult& ocr, float min_confidence) {
    std::map<std::tuple<int, int, int>, std::vector<const OcrWord*>> grouped;
    for (const OcrWord& word : ocr.words) {
        if (word.text.empty() || word.box.empty()) continue;
        if (word.confidence >= 0.0F && word.confidence < min_confidence) continue;
        grouped[{word.block, word.paragraph, word.line}].push_back(&word);
    }
    std::vector<LineWords> lines;
    lines.reserve(grouped.size());
    for (auto& [key, words] : grouped) {
        static_cast<void>(key);
        std::stable_sort(words.begin(), words.end(),
                         [](const OcrWord* a, const OcrWord* b) { return a->box.x < b->box.x; });
        LineWords line;
        for (const OcrWord* word : words) {
            if (!line.text.empty()) line.text.push_back(' ');
            const std::size_t begin = line.text.size();
            line.text += word->text;
            line.spans.emplace_back(begin, line.text.size(), word->box);
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

[[nodiscard]] Box box_for_span(const LineWords& line, std::size_t begin, std::size_t end) {
    Box result;
    for (const auto& [word_begin, word_end, box] : line.spans) {
        if (word_begin < end && begin < word_end) result = box_union(result, box);
    }
    if (result.empty() && !line.spans.empty()) {
        // A span covering only separators: cover the nearest following word.
        for (const auto& [word_begin, word_end, box] : line.spans) {
            static_cast<void>(word_end);
            if (word_begin >= begin) return box;
            result = box;
        }
    }
    return result;
}

[[nodiscard]] bool contains_icase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    auto lower = [](char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); };
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (lower(haystack[i + j]) != lower(needle[j])) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

struct Pattern {
    SecretKind kind;
    std::regex expression;
    int secret_group;                 // capture group holding the secret, 0 = whole match
    std::vector<std::string> prefilters;  // any-of substrings; empty = always run
    bool (*validate)(std::string_view secret) = nullptr;  // optional extra check on the group
};

// OCR frequently reads '=' as '-'. A hyphen-separated label is therefore
// accepted only when the value does not look like an ordinary word (mixed
// case or a digit), so prose like "password-protected" stays untouched.
[[nodiscard]] bool value_looks_secret(std::string_view value) noexcept {
    if (value.size() < 6) return false;
    bool lower = false, upper = false, digit = false;
    for (char ch : value) {
        if (ch >= 'a' && ch <= 'z') lower = true;
        if (ch >= 'A' && ch <= 'Z') upper = true;
        if (is_digit(ch)) digit = true;
    }
    return (lower && upper) || digit;
}

[[nodiscard]] const std::vector<Pattern>& line_patterns() {
    static const std::vector<Pattern> patterns = [] {
        using flag = std::regex::flag_type;
        constexpr flag kIcase = std::regex::ECMAScript | std::regex::icase;
        constexpr flag kPlain = std::regex::ECMAScript;
        std::vector<Pattern> all;
        auto add = [&all](SecretKind kind, const char* expr, int group, flag flags,
                          std::vector<std::string> pre = {},
                          bool (*validate)(std::string_view) = nullptr) {
            all.push_back(
                Pattern{kind, std::regex(expr, flags), group, std::move(pre), validate});
        };
        add(SecretKind::kPrivateKey, R"(-{3,}BEGIN [A-Z ]*PRIVATE KEY-{3,})", 0, kPlain, {"BEGIN"});
        // OCR often drops or mangles the dash runs around the header.
        add(SecretKind::kPrivateKey, R"(\bBEGIN [A-Z ]*PRIVATE KEY\b)", 0, kPlain, {"BEGIN"});
        add(SecretKind::kAwsAccessKey, R"(\b(?:AKIA|ASIA)[0-9A-Z]{16}\b)", 0, kPlain,
            {"AKIA", "ASIA"});
        add(SecretKind::kGcpApiKey, R"(\bAIza[0-9A-Za-z_-]{35}\b)", 0, kPlain, {"AIza"});
        add(SecretKind::kGitHubToken, R"(\b(?:ghp|gho|ghu|ghs|ghr)_[0-9A-Za-z]{36,}\b)", 0,
            kPlain, {"gh"});
        add(SecretKind::kGitHubToken, R"(\bgithub_pat_[0-9A-Za-z_]{22,}\b)", 0, kPlain,
            {"github_pat_"});
        add(SecretKind::kSlackToken, R"(\bxox[baprs]-[0-9A-Za-z-]{10,}\b)", 0, kPlain, {"xox"});
        add(SecretKind::kJwt,
            R"(\beyJ[0-9A-Za-z_-]{8,}\.[0-9A-Za-z_-]{8,}\.[0-9A-Za-z_-]{8,}\b)", 0, kPlain,
            {"eyJ"});
        add(SecretKind::kBearerToken, R"(Bearer\s+([0-9A-Za-z._~+/=-]{12,}))", 1, kIcase,
            {"Bearer", "bearer", "BEARER"});
        add(SecretKind::kConnectionString,
            R"(\b(?:postgres(?:ql)?|mysql|mongodb(?:\+srv)?|amqps?|rediss?|mssql|sqlserver)://[^\s/@:]+:[^\s/@]+@[^\s]+)",
            0, kIcase, {"://"});
        // OCR inserts spaces around ':', '@', '.' and '/' in URIs. The
        // scheme list is closed, so tolerating spaces here stays precise.
        add(SecretKind::kConnectionString,
            R"(\b(?:postgres(?:ql)?|mysql|mongodb(?:\+srv)?|amqps?|rediss?|mssql|sqlserver)\s*:\s*/\s*/\s*[^\s/@:]{1,64}\s*:\s*[^\s/@]{1,64}\s*@\s*[A-Za-z0-9_-]+(?:\s*\.\s*[A-Za-z0-9_-]+)*(?:\s*:\s*[0-9]{1,5})?(?:/[^\s]*)?)",
            0, kIcase, {"://", ": /", ":/"});
        add(SecretKind::kLabelledSecret,
            R"(\b(?:password|passwd|pwd|client[_-]?secret|api[_-]?key|access[_-]?key|secret[_-]?key|access[_-]?token|auth[_-]?token|accountkey|shared[_-]?secret|secret)\s*[:=]\s*["']?([^\s"';,]{4,}))",
            1, kIcase, {"=", ":"});
        add(SecretKind::kLabelledSecret, R"(\btoken\s*[:=]\s*["']?([^\s"';,]{8,}))", 1, kIcase,
            {"token", "Token", "TOKEN"});
        add(SecretKind::kLabelledSecret,
            R"(\b(?:password|passwd|pwd|client[_-]?secret|api[_-]?key|secret[_-]?key|access[_-]?token|auth[_-]?token)\s*-\s*["']?([^\s"';,]{6,}))",
            1, kIcase, {"-", "="}, &value_looks_secret);
        return all;
    }();
    return patterns;
}

struct Candidate {
    SecretKind kind;
    std::size_t begin;
    std::size_t end;
};

[[nodiscard]] bool ssn_valid(std::string_view text) noexcept {
    if (text.size() != 11) return false;
    auto num = [&text](std::size_t offset, std::size_t length) noexcept {
        int value = 0;
        for (std::size_t i = 0; i < length; ++i) value = value * 10 + (text[offset + i] - '0');
        return value;
    };
    const int area = num(0, 3);
    const int group = num(4, 2);
    const int serial = num(7, 4);
    if (area == 0 || area == 666 || area >= 900) return false;
    return group >= 1 && serial >= 1;
}

[[nodiscard]] bool ipv6_acceptable(std::string_view text) noexcept {
    if (text == "::1" || text == "::") return false;  // loopback/unspecified are not sensitive
    int colons = 0;
    bool compressed = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ':') {
            ++colons;
            if (i + 1 < text.size() && text[i + 1] == ':') compressed = true;
        }
    }
    // Full form has 7 colons; compressed forms carry "::". Anything else is
    // most likely a time (12:30:45) or a MAC-style/time fragment.
    return compressed || colons == 7;
}

[[nodiscard]] bool high_entropy_acceptable(std::string_view token, const DetectorConfig& config) {
    if (token.size() < config.high_entropy_min_length) return false;
    bool has_lower = false, has_upper = false, has_digit = false;
    for (char ch : token) {
        if (ch >= 'a' && ch <= 'z') has_lower = true;
        if (ch >= 'A' && ch <= 'Z') has_upper = true;
        if (is_digit(ch)) has_digit = true;
    }
    if (!has_lower || !has_upper || !has_digit) return false;
    return shannon_entropy_bits_per_char(token) >= config.high_entropy_min_bits_per_char;
}

// OCR turns underscores into spaces and splits long tokens into fragments
// ("ghp_ABC..." arrives as "ghp ABC DEF..."). For the closed set of vendor
// prefixes, a hand-rolled scanner tolerates a few single spaces inside the
// token and an underscore read as a space. The spaced path additionally
// requires a digit in the payload so prose starting with the same letters
// is not swallowed. The contiguous regexes above remain the primary path.
struct SpacedTokenSpec {
    SecretKind kind;
    const char* prefix;      // '_' in the prefix matches '_' or ' '
    std::size_t min_payload; // payload characters, spaces excluded
    std::size_t max_payload;
    bool (*payload_char)(char) noexcept;
};

[[nodiscard]] bool is_alnum_char(char ch) noexcept {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}
[[nodiscard]] bool is_upper_alnum_char(char ch) noexcept {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z');
}
[[nodiscard]] bool is_b64url_char(char ch) noexcept {
    return is_alnum_char(ch) || ch == '_' || ch == '-';
}
[[nodiscard]] bool is_pat_char(char ch) noexcept { return is_alnum_char(ch) || ch == '_'; }
[[nodiscard]] bool is_slack_char(char ch) noexcept { return is_alnum_char(ch) || ch == '-'; }

void scan_spaced_tokens(const LineWords& line, std::vector<Candidate>& out) {
    static constexpr SpacedTokenSpec kSpecs[] = {
        {SecretKind::kGitHubToken, "ghp_", 36, 80, &is_alnum_char},
        {SecretKind::kGitHubToken, "gho_", 36, 80, &is_alnum_char},
        {SecretKind::kGitHubToken, "ghu_", 36, 80, &is_alnum_char},
        {SecretKind::kGitHubToken, "ghs_", 36, 80, &is_alnum_char},
        {SecretKind::kGitHubToken, "ghr_", 36, 80, &is_alnum_char},
        {SecretKind::kGitHubToken, "github_pat_", 22, 120, &is_pat_char},
        {SecretKind::kAwsAccessKey, "AKIA", 16, 16, &is_upper_alnum_char},
        {SecretKind::kAwsAccessKey, "ASIA", 16, 16, &is_upper_alnum_char},
        {SecretKind::kGcpApiKey, "AIza", 35, 35, &is_b64url_char},
        {SecretKind::kSlackToken, "xoxb-", 10, 100, &is_slack_char},
        {SecretKind::kSlackToken, "xoxa-", 10, 100, &is_slack_char},
        {SecretKind::kSlackToken, "xoxp-", 10, 100, &is_slack_char},
        {SecretKind::kSlackToken, "xoxr-", 10, 100, &is_slack_char},
        {SecretKind::kSlackToken, "xoxs-", 10, 100, &is_slack_char},
    };
    const std::string& text = line.text;
    for (const SpacedTokenSpec& spec : kSpecs) {
        const std::string_view prefix(spec.prefix);
        // Find candidate starts by the first two literal characters.
        for (std::size_t start = 0; start + prefix.size() <= text.size(); ++start) {
            if (text[start] != prefix[0] || text[start + 1] != prefix[1]) continue;
            if (start > 0 && is_alnum_char(text[start - 1])) continue;
            std::size_t pos = start;
            bool prefix_ok = true;
            for (char expected : prefix) {
                const char actual = text[pos];
                if (expected == '_' || expected == '-') {
                    if (actual != expected && actual != ' ') { prefix_ok = false; break; }
                } else if (actual != expected) {
                    prefix_ok = false;
                    break;
                }
                ++pos;
            }
            if (!prefix_ok) continue;
            std::size_t payload = 0;
            int spaces = 0;
            bool has_digit = false;
            std::size_t end = pos;
            while (end < text.size()) {
                const char ch = text[end];
                if (spec.payload_char(ch)) {
                    ++payload;
                    if (is_digit(ch)) has_digit = true;
                    ++end;
                } else if (ch == ' ' && spaces < 3 && end + 1 < text.size() &&
                           spec.payload_char(text[end + 1])) {
                    ++spaces;
                    ++end;
                } else {
                    break;
                }
                if (payload > spec.max_payload) break;
            }
            if (payload >= spec.min_payload && payload <= spec.max_payload && has_digit &&
                spaces > 0) {
                out.push_back(Candidate{spec.kind, start, end});
            }
        }
    }
}

void scan_validated_patterns(const LineWords& line, const DetectorConfig& config,
                             std::vector<Candidate>& out) {
    static const std::regex kEmail(R"(\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b)");
    static const std::regex kIpv4(R"(\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b)");
    static const std::regex kIpv6(
        R"((?:[0-9A-Fa-f]{1,4}:){2,7}[0-9A-Fa-f]{1,4}|[0-9A-Fa-f]{1,4}(?::[0-9A-Fa-f]{0,4})*::(?:[0-9A-Fa-f]{1,4}(?::[0-9A-Fa-f]{1,4})*)?|::(?:[0-9A-Fa-f]{1,4}:)*[0-9A-Fa-f]{1,4})");
    static const std::regex kCard(R"(\b[0-9](?:[ -]?[0-9]){12,18}\b)");
    static const std::regex kIban(R"(\b[A-Z]{2}[0-9]{2}(?: ?[A-Z0-9]{4}){2,6}(?: ?[A-Z0-9]{1,3})?\b)");
    static const std::regex kSsn(R"(\b[0-9]{3}-[0-9]{2}-[0-9]{4}\b)");
    static const std::regex kEntropyToken(R"([0-9A-Za-z+/=_-]{20,})");

    auto scan = [&line, &out](const std::regex& expr, SecretKind kind, const auto& validate) {
        for (std::sregex_iterator it(line.text.begin(), line.text.end(), expr), end; it != end;
             ++it) {
            const std::smatch& match = *it;
            const auto begin = static_cast<std::size_t>(match.position(0));
            const std::string_view text(line.text.data() + match.position(0),
                                        static_cast<std::size_t>(match.length(0)));
            if (validate(text)) out.push_back(Candidate{kind, begin, begin + text.size()});
        }
    };

    if (config.detect_emails && line.text.find('@') != std::string::npos) {
        scan(kEmail, SecretKind::kEmail, [](std::string_view) { return true; });
    }
    if (config.ip_mode != IpMode::kOff && line.text.find('.') != std::string::npos) {
        for (std::sregex_iterator it(line.text.begin(), line.text.end(), kIpv4), end_it;
             it != end_it; ++it) {
            const std::smatch& match = *it;
            const std::string_view text(line.text.data() + match.position(0),
                                        static_cast<std::size_t>(match.length(0)));
            bool ok = false;
            static_cast<void>(parse_ipv4(text, ok));
            if (!ok) continue;
            const bool private_ip = is_private_ipv4(text);
            if (config.ip_mode == IpMode::kPrivateOnly && !private_ip) continue;
            const auto begin = static_cast<std::size_t>(match.position(0));
            out.push_back(Candidate{private_ip ? SecretKind::kPrivateIpv4 : SecretKind::kIpv4,
                                    begin, begin + text.size()});
        }
    }
    if (config.detect_ipv6 && line.text.find(':') != std::string::npos) {
        scan(kIpv6, SecretKind::kIpv6, [](std::string_view text) { return ipv6_acceptable(text); });
    }
    if (config.detect_credit_cards) {
        scan(kCard, SecretKind::kCreditCard, [](std::string_view text) {
            std::string digits;
            for (char ch : text) {
                if (is_digit(ch)) digits.push_back(ch);
            }
            return luhn_valid(digits);
        });
    }
    if (config.detect_ibans) {
        scan(kIban, SecretKind::kIban, [](std::string_view text) { return iban_valid(text); });
    }
    if (config.detect_ssn) {
        scan(kSsn, SecretKind::kSsn, [](std::string_view text) { return ssn_valid(text); });
    }
    if (config.detect_high_entropy) {
        scan(kEntropyToken, SecretKind::kHighEntropyToken, [&config](std::string_view text) {
            return high_entropy_acceptable(text, config);
        });
    }
    scan_spaced_tokens(line, out);
}

// OCR sometimes inserts a space inside a long token. For lines carrying a
// vendor prefix hint, each single-space removal is tried as a variant (all
// other word boundaries stay intact) and match spans are mapped back to
// the original character offsets.
// A following/preceding fragment counts as a token continuation when it is
// made only of token characters and does not look like an ordinary word
// (digit, separator symbol or mixed case required). This keeps prose such
// as "please" after an email address from being swallowed.
[[nodiscard]] bool is_token_continuation(std::string_view fragment) noexcept {
    if (fragment.size() < 3) return false;
    bool lower = false, upper = false, digit = false, symbol = false;
    for (char ch : fragment) {
        if (ch >= 'a' && ch <= 'z') {
            lower = true;
        } else if (ch >= 'A' && ch <= 'Z') {
            upper = true;
        } else if (is_digit(ch)) {
            digit = true;
        } else if (ch == '.' || ch == ':' || ch == '-' || ch == '_' || ch == '/' || ch == '+' ||
                   ch == '=' || ch == '@' || ch == '~' || ch == '%') {
            symbol = true;
        } else {
            return false;
        }
    }
    return digit || symbol || (lower && upper);
}

// A pattern match often stops at an OCR-inserted space inside the secret.
// Grow the span across adjacent token-like fragments (in both directions)
// so the painted box covers the whole value, not just its first fragment.
void expand_candidate_span(const LineWords& line, Candidate& candidate) noexcept {
    const std::string& text = line.text;
    // Snap to the containing word first: matches that start or end in the
    // middle of a word (common for IPv6 fragments) must claim the whole
    // word before neighbouring fragments are considered.
    while (candidate.begin > 0 && text[candidate.begin - 1] != ' ') --candidate.begin;
    while (candidate.end < text.size() && text[candidate.end] != ' ') ++candidate.end;
    while (candidate.end < text.size() && text[candidate.end] == ' ') {
        std::size_t fragment_begin = candidate.end + 1;
        std::size_t fragment_end = fragment_begin;
        while (fragment_end < text.size() && text[fragment_end] != ' ') ++fragment_end;
        if (!is_token_continuation(
                std::string_view(text).substr(fragment_begin, fragment_end - fragment_begin))) {
            break;
        }
        candidate.end = fragment_end;
    }
    while (candidate.begin > 0 && text[candidate.begin - 1] == ' ') {
        std::size_t fragment_end = candidate.begin - 1;
        std::size_t fragment_begin = fragment_end;
        while (fragment_begin > 0 && text[fragment_begin - 1] != ' ') --fragment_begin;
        if (!is_token_continuation(
                std::string_view(text).substr(fragment_begin, fragment_end - fragment_begin))) {
            break;
        }
        candidate.begin = fragment_begin;
    }
}

void scan_compact_patterns(const LineWords& line, std::vector<Candidate>& out) {
    struct Hint {
        SecretKind kind;
        const char* needle;
    };
    static constexpr Hint kHints[] = {{SecretKind::kAwsAccessKey, "AKIA"},
                                      {SecretKind::kAwsAccessKey, "ASIA"},
                                      {SecretKind::kGcpApiKey, "AIza"},
                                      {SecretKind::kGitHubToken, "ghp_"},
                                      {SecretKind::kGitHubToken, "gho_"},
                                      {SecretKind::kGitHubToken, "github_pat_"},
                                      {SecretKind::kSlackToken, "xox"},
                                      {SecretKind::kJwt, "eyJ"}};
    std::vector<const Pattern*> patterns;
    for (const Hint& hint : kHints) {
        if (line.text.find(hint.needle) == std::string::npos) continue;
        for (const Pattern& pattern : line_patterns()) {
            if (pattern.kind == hint.kind && pattern.secret_group == 0) patterns.push_back(&pattern);
        }
    }
    if (patterns.empty()) return;
    for (std::size_t removed = 0; removed < line.text.size(); ++removed) {
        if (line.text[removed] != ' ' && line.text[removed] != '\t') continue;
        std::string variant = line.text;
        variant.erase(removed, 1);
        for (const Pattern* pattern : patterns) {
            for (std::sregex_iterator it(variant.begin(), variant.end(), pattern->expression), end;
                 it != end; ++it) {
                const std::smatch& match = *it;
                const auto begin = static_cast<std::size_t>(match.position(0));
                const auto length = static_cast<std::size_t>(match.length(0));
                if (length == 0 || begin > removed || begin + length <= removed) continue;
                // Span crosses the removed space: shift the tail back by one.
                out.push_back(Candidate{pattern->kind, begin, begin + length + 1});
            }
        }
    }
}
void scan_line_patterns(const LineWords& line, const DetectorConfig& config,
                        std::vector<Candidate>& out) {
    static_cast<void>(config);
    // ADO.NET / Azure style connection strings: a line full of key=value
    // pairs that carries a password-like key is sensitive as a whole
    // (server, user and database names are part of the leak). OCR often
    // reads '=' as '-', so key-value fragments joined by either character
    // count, but at least two semicolons plus two fragments are required
    // before a line is treated as a connection string.
    const auto semicolons =
        static_cast<std::size_t>(std::count(line.text.begin(), line.text.end(), ';'));
    const bool password_key = contains_icase(line.text, "password") ||
                              contains_icase(line.text, "pwd") ||
                              contains_icase(line.text, "accountkey") ||
                              contains_icase(line.text, "accesskey");
    if (semicolons >= 2 && password_key) {
        static const std::regex kFragment(R"([A-Za-z][A-Za-z ]{1,20}[-=][^\s;]{2,})");
        std::size_t fragments = 0;
        for (std::sregex_iterator it(line.text.begin(), line.text.end(), kFragment), end;
             it != end; ++it) {
            ++fragments;
        }
        if (line.text.find('=') != std::string::npos || fragments >= 2) {
            const std::size_t begin = line.text.find_first_not_of(" \t");
            const std::size_t end = line.text.find_last_not_of(" \t") + 1;
            out.push_back(Candidate{SecretKind::kConnectionString, begin, end});
        }
    }
    for (const Pattern& pattern : line_patterns()) {
        if (!pattern.prefilters.empty()) {
            const bool any = std::any_of(pattern.prefilters.begin(), pattern.prefilters.end(),
                                         [&line](const std::string& needle) {
                                             return line.text.find(needle) != std::string::npos;
                                         });
            if (!any) continue;
        }
        for (std::sregex_iterator it(line.text.begin(), line.text.end(), pattern.expression), end;
             it != end; ++it) {
            const std::smatch& match = *it;
            if (pattern.secret_group < 0) continue;
            const auto group = static_cast<std::size_t>(pattern.secret_group);
            if (group >= match.size() || !match[group].matched) continue;
            const auto begin = static_cast<std::size_t>(match.position(group));
            const auto length = static_cast<std::size_t>(match.length(group));
            if (length == 0) continue;
            if (pattern.validate != nullptr &&
                !pattern.validate(std::string_view(line.text.data() + begin, length))) {
                continue;
            }
            out.push_back(Candidate{pattern.kind, begin, begin + length});
        }
    }
    scan_compact_patterns(line, out);
}

}  // namespace

std::vector<Finding> detect_secrets(const OcrResult& ocr, const DetectorConfig& config) {
    const std::vector<LineWords> lines = build_lines(ocr, config.min_word_confidence);
    std::vector<Finding> findings;
    std::vector<Candidate> candidates;
    for (const LineWords& line : lines) {
        candidates.clear();
        try {
            scan_line_patterns(line, config, candidates);
            scan_validated_patterns(line, config, candidates);
        } catch (const std::regex_error&) {
            continue;  // a pathological line must not abort the whole scan
        }
        for (Candidate candidate : candidates) {
            expand_candidate_span(line, candidate);
            Box box = box_for_span(line, candidate.begin, candidate.end);
            if (!box.empty()) {
                findings.push_back(Finding{candidate.kind, box, candidate.end - candidate.begin});
            }
        }
    }
    return dedupe_findings(std::move(findings));
}

std::vector<Finding> dedupe_findings(std::vector<Finding> findings) {
    // Collapse duplicates: keep the largest box, drop any finding mostly
    // covered by an already kept one (patterns frequently overlap, e.g. a
    // labelled GitHub token matches both the labelled and GitHub rules).
    std::sort(findings.begin(), findings.end(), [](const Finding& a, const Finding& b) {
        return a.box.area() > b.box.area();
    });
    std::vector<Finding> kept;
    kept.reserve(findings.size());
    for (const Finding& finding : findings) {
        bool covered = false;
        for (const Finding& other : kept) {
            if (!boxes_overlap(finding.box, other.box)) continue;
            const int ix0 = std::max(finding.box.x, other.box.x);
            const int iy0 = std::max(finding.box.y, other.box.y);
            const int ix1 = std::min(finding.box.right(), other.box.right());
            const int iy1 = std::min(finding.box.bottom(), other.box.bottom());
            const auto intersection =
                static_cast<std::int64_t>(ix1 - ix0) * static_cast<std::int64_t>(iy1 - iy0);
            if (finding.box.area() > 0 && intersection * 2 >= finding.box.area()) {
                covered = true;
                break;
            }
        }
        if (!covered) kept.push_back(finding);
    }
    std::sort(kept.begin(), kept.end(), [](const Finding& a, const Finding& b) {
        if (a.box.y != b.box.y) return a.box.y < b.box.y;
        return a.box.x < b.box.x;
    });
    return kept;
}

}  // namespace secretsimage
