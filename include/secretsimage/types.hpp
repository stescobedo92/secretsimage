#pragma once

#include <algorithm>
#include <cstdint>

namespace secretsimage {

// Axis-aligned rectangle in image pixel coordinates. (x, y) is the
// top-left corner; width/height may be zero for an empty box.
struct Box {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] constexpr int right() const noexcept { return x + width; }
    [[nodiscard]] constexpr int bottom() const noexcept { return y + height; }
    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
    [[nodiscard]] constexpr std::int64_t area() const noexcept {
        return empty() ? 0 : static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height);
    }
    constexpr bool operator==(const Box&) const noexcept = default;
};

[[nodiscard]] inline Box box_union(const Box& a, const Box& b) noexcept {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const int x0 = std::min(a.x, b.x);
    const int y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.right(), b.right());
    const int y1 = std::max(a.bottom(), b.bottom());
    return Box{x0, y0, x1 - x0, y1 - y0};
}

[[nodiscard]] inline bool boxes_overlap(const Box& a, const Box& b) noexcept {
    return !a.empty() && !b.empty() && a.x < b.right() && b.x < a.right() && a.y < b.bottom() &&
           b.y < a.bottom();
}

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
    constexpr bool operator==(const Color&) const noexcept = default;
};

// Categories of sensitive content the detector can report. Names are part of
// the audit log schema; do not rename existing values.
enum class SecretKind {
    kPrivateKey,
    kAwsAccessKey,
    kGcpApiKey,
    kGitHubToken,
    kSlackToken,
    kJwt,
    kBearerToken,
    kConnectionString,
    kLabelledSecret,
    kEmail,
    kPrivateIpv4,
    kIpv4,
    kIpv6,
    kCreditCard,
    kIban,
    kSsn,
    kHighEntropyToken,
};

[[nodiscard]] constexpr const char* to_string(SecretKind kind) noexcept {
    switch (kind) {
        case SecretKind::kPrivateKey: return "private_key";
        case SecretKind::kAwsAccessKey: return "aws_access_key";
        case SecretKind::kGcpApiKey: return "gcp_api_key";
        case SecretKind::kGitHubToken: return "github_token";
        case SecretKind::kSlackToken: return "slack_token";
        case SecretKind::kJwt: return "jwt";
        case SecretKind::kBearerToken: return "bearer_token";
        case SecretKind::kConnectionString: return "connection_string";
        case SecretKind::kLabelledSecret: return "labelled_secret";
        case SecretKind::kEmail: return "email";
        case SecretKind::kPrivateIpv4: return "private_ipv4";
        case SecretKind::kIpv4: return "ipv4";
        case SecretKind::kIpv6: return "ipv6";
        case SecretKind::kCreditCard: return "credit_card";
        case SecretKind::kIban: return "iban";
        case SecretKind::kSsn: return "ssn";
        case SecretKind::kHighEntropyToken: return "high_entropy_token";
    }
    return "high_entropy_token";
}

}  // namespace secretsimage
