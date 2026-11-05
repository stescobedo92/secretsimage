#include <string>
#include <vector>

#include "secretsimage/detector.hpp"
#include "test_framework.hpp"

namespace {

using namespace secretsimage;  // NOLINT -- test translation unit

// Builds OCR words for one or more text lines with deterministic geometry
// (10 px per character, one 18 px line every 26 px).
OcrResult ocr_from_lines(const std::vector<std::string>& lines) {
    OcrResult result;
    result.image_width = 4000;
    result.image_height = static_cast<int>(lines.size()) * 26 + 20;
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const std::string& line = lines[line_index];
        std::size_t pos = 0;
        while (pos < line.size()) {
            if (line[pos] == ' ') { ++pos; continue; }
            const std::size_t begin = pos;
            while (pos < line.size() && line[pos] != ' ') ++pos;
            OcrWord word;
            word.text = line.substr(begin, pos - begin);
            word.box = Box{static_cast<int>(begin) * 10, static_cast<int>(line_index) * 26,
                           static_cast<int>(pos - begin) * 10, 18};
            word.confidence = 95.0F;
            word.block = 1;
            word.paragraph = 1;
            word.line = static_cast<int>(line_index) + 1;
            result.words.push_back(std::move(word));
        }
    }
    return result;
}

bool has_kind(const std::vector<Finding>& findings, SecretKind kind) {
    for (const Finding& finding : findings) {
        if (finding.kind == kind) return true;
    }
    return false;
}

std::vector<Finding> detect(const std::string& line, const DetectorConfig& config = {}) {
    return detect_secrets(ocr_from_lines({line}), config);
}

}  // namespace

TEST_CASE("detector flags vendor token and key formats") {
    CHECK(has_kind(detect("AWS_ACCESS_KEY_ID=AKIAIOSFODNN7EXAMPLE"), SecretKind::kAwsAccessKey));
    CHECK(has_kind(detect("key AIzaSyD4iK8xQ2mP9vR7tW3yZ6bN5cA1sEfGhJk here"), SecretKind::kGcpApiKey));
    CHECK(has_kind(detect("token ghp_1234567890abcdefghijklmnopqrstuvwxyz done"),
                   SecretKind::kGitHubToken));
    CHECK(has_kind(detect("export TOKEN=xoxb-1234567890-abcdefgh"), SecretKind::kSlackToken));
    CHECK(has_kind(detect("-----BEGIN RSA PRIVATE KEY-----"), SecretKind::kPrivateKey));
}

TEST_CASE("detector flags JWT and bearer tokens") {
    const std::string jwt =
        "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c";
    CHECK(has_kind(detect("jwt: " + jwt), SecretKind::kJwt));
    CHECK(has_kind(detect("Authorization: Bearer abcDEF1234567890xyz"), SecretKind::kBearerToken));
}

TEST_CASE("detector flags connection strings as a whole") {
    CHECK(has_kind(detect("url postgres://svc:s3cr3tPass@db.internal:5432/app"),
                   SecretKind::kConnectionString));
    const std::vector<Finding> adonet =
        detect("Server=db01.corp.local;Database=erp;User Id=svc;Password=Hunter2!x");
    CHECK(has_kind(adonet, SecretKind::kConnectionString));
    CHECK_MSG(adonet.front().box.width > 400, "whole connection string line is covered");
}

TEST_CASE("detector flags labelled secrets and PII") {
    CHECK(has_kind(detect("password = Sunflower#42"), SecretKind::kLabelledSecret));
    CHECK(has_kind(detect("client_secret: kX9vQ2mP8zR4"), SecretKind::kLabelledSecret));
    CHECK(has_kind(detect("mail maria.garcia@example.com please"), SecretKind::kEmail));
    CHECK(has_kind(detect("host 192.168.10.25 is down"), SecretKind::kPrivateIpv4));
    CHECK(has_kind(detect("peer 2001:db8:85a3::8a2e dropped"), SecretKind::kIpv6));
    CHECK(has_kind(detect("card 4111 1111 1111 1111 expired"), SecretKind::kCreditCard));
    CHECK(has_kind(detect("iban GB29 NWBK 6016 1331 9268 19 ok"), SecretKind::kIban));
    CHECK(has_kind(detect("ssn 123-45-6789 on file"), SecretKind::kSsn));
}

TEST_CASE("detector validators reject look-alikes") {
    CHECK(luhn_valid("4111111111111111"));
    CHECK(!luhn_valid("4111111111111112"));
    CHECK(iban_valid("GB29 NWBK 6016 1331 9268 19"));
    CHECK(!iban_valid("GB29 NWBK 6016 1331 9268 18"));
    CHECK(is_private_ipv4("10.0.0.5"));
    CHECK(is_private_ipv4("172.16.8.9"));
    CHECK(!is_private_ipv4("172.32.8.9"));
    CHECK(!is_private_ipv4("8.8.8.8"));
    CHECK(!is_private_ipv4("999.1.1.1"));
    CHECK(shannon_entropy_bits_per_char("aaaaaaaa") == 0.0);
    // Same shapes as the real thing, but failing the checksum/structure.
    CHECK(detect("card 4111 1111 1111 1112 expired").empty());
    CHECK(detect("ssn 000-45-6789 on file").empty());
    CHECK(detect("ssn 666-45-6789 on file").empty());
}

TEST_CASE("detector leaves clean text alone") {
    CHECK(detect("Build finished in 42 seconds with 0 warnings").empty());
    CHECK(detect("commit f56fb4ed266f49db1c240159584cd4170ac713b2 merged").empty());
    CHECK(detect("request id 550e8400-e29b-41d4-a716-446655440000 done").empty());
    CHECK(detect("deploy at 12:30:45 UTC version 1.4.2").empty());
    CHECK(detect("Enter your password:").empty());
    CHECK(detect("Order 1234 5678 9012 shipped on Tuesday").empty());
    // Public IPs are not sensitive under the default private-only policy.
    CHECK(detect("public resolver 8.8.8.8 answered").empty());
    DetectorConfig all_ips;
    all_ips.ip_mode = IpMode::kAll;
    CHECK(has_kind(detect("public resolver 8.8.8.8 answered", all_ips), SecretKind::kIpv4));
}

TEST_CASE("detector covers secrets split across OCR words") {
    const std::vector<Finding> findings = detect("card 4111 1111 1111 1111 expired");
    CHECK(findings.size() == 1);
    // Four digit groups plus spaces: the finding box spans all four words.
    CHECK_MSG(findings[0].box.width >= 180, "box spans every digit group");
}

TEST_CASE("detector de-duplicates overlapping pattern hits") {
    const std::vector<Finding> findings =
        detect("api_key: ghp_1234567890abcdefghijklmnopqrstuvwxyz");
    CHECK_MSG(findings.size() == 1, "labelled + GitHub rules collapse into one finding");
}

TEST_CASE("detector rejoins vendor tokens whose underscore OCR read as space") {
    // Observed against Tesseract 5.4: "ghp_<36>" arrived as three fragments.
    CHECK(has_kind(detect("export GITHUB TOKEN=ghp Dox78GY1B2Hka38daoV8BFP pYHm0g4YQ4PLF done"),
                   SecretKind::kGitHubToken));
    CHECK(detect("the ghp report is ready for the team meeting today").empty());
    CHECK(detect("ghp documentation covers everything needed for onboarding").empty());
}

TEST_CASE("detector handles OCR-split IPv6 and spaced URIs") {
    const std::vector<Finding> ipv6 = detect("ipv6 peer 2001: db8:85a3:21c0::5da dropped");
    CHECK(has_kind(ipv6, SecretKind::kIpv6));
    CHECK_MSG(ipv6[0].box.x == 100, "box starts at the first IPv6 fragment");
    CHECK(has_kind(detect("DATABASE_URL=postgres://svc_app: jVI4LUFb0m@db. internal :5432/billing"),
                   SecretKind::kConnectionString));
}

TEST_CASE("detector expands findings across OCR-split token fragments") {
    // The labelled value match stops at the inserted space; the finding box
    // must still cover the trailing fragment, but not ordinary prose.
    const std::vector<Finding> findings = detect("client_secret: kX9vQ2 mP8zR4wT today");
    CHECK(findings.size() == 1);
    CHECK_MSG(findings[0].box.right() == 300, "box covers both fragments, stops before today");
    const std::vector<Finding> email = detect("mail maria.garcia@example.com please call");
    CHECK(email.size() == 1);
    CHECK_MSG(email[0].box.right() == 290, "email box does not swallow the next word");
}

TEST_CASE("detector rejoins tokens split by an OCR-inserted space") {
    CHECK(has_kind(detect("export TOKEN=ghp_1234567890abc defghijklmnopqrstuvwxyz done"),
                   SecretKind::kGitHubToken));
    CHECK(has_kind(detect("key AKIAIOSFOD NN7EXAMPLE here"), SecretKind::kAwsAccessKey));
    // Space-stripping must not fuse two clean words into a token look-alike:
    CHECK(detect("the ghp report lists abcdefghijklmnopqrstuvwxyz items").empty());
}

TEST_CASE("detector tolerates OCR reading equals as hyphen") {
    // As observed against a real engine: "Password=VVwYmq1cEt!" came back
    // as "Password-UUwYmqicEt". The hyphen form fires only for values that
    // do not look like ordinary words.
    CHECK(has_kind(detect("the Password-UUwYmqicEt9 value here"), SecretKind::kLabelledSecret));
    CHECK(detect("this file is password-protected and stays").empty());
    CHECK(has_kind(detect("Server-db01.corp;Database-erp;User Id-svc;Password-Abcdef12"),
                   SecretKind::kConnectionString));
}

TEST_CASE("detector high entropy rule catches unknown vendor keys only") {
    const std::string unknown_key = "kX9vQ2mP8zR4wT7yU3sA6dF1gH0jL5nB";
    CHECK(has_kind(detect("key " + unknown_key + " end"), SecretKind::kHighEntropyToken));
    DetectorConfig disabled;
    disabled.detect_high_entropy = false;
    CHECK(detect("key " + unknown_key + " end", disabled).empty());
}
