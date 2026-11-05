#pragma once

#include <cstddef>
#include <vector>

#include "secretsimage/ocr.hpp"
#include "secretsimage/types.hpp"

namespace secretsimage {

enum class IpMode {
    kOff,
    kPrivateOnly,  // RFC 1918 + loopback + link-local (default)
    kAll,
};

struct DetectorConfig {
    bool detect_emails = true;
    bool detect_credit_cards = true;   // Luhn-validated
    bool detect_ibans = true;          // ISO 13616 mod-97 validated
    bool detect_ssn = true;            // US Social Security number shape
    IpMode ip_mode = IpMode::kPrivateOnly;
    bool detect_ipv6 = true;
    // Generic high-entropy tokens catch vendor key formats with no fixed
    // prefix. The entropy and character-class gates keep git SHAs, UUIDs
    // and hashes (at most ~4 bit/char) from being over-redacted.
    bool detect_high_entropy = true;
    std::size_t high_entropy_min_length = 24;
    double high_entropy_min_bits_per_char = 4.5;
    // OCR words below this confidence (0..100) are still scanned: a missed
    // secret costs far more than a false positive. Kept for tuning only.
    float min_word_confidence = 0.0F;
};

// A detected sensitive region. Deliberately stores no matched text: only
// its kind, location and length ever reach logs or reports.
struct Finding {
    SecretKind kind = SecretKind::kHighEntropyToken;
    Box box;
    std::size_t matched_length = 0;  // characters, for the audit log only
};

// Scans OCR output line by line. Findings are de-duplicated so overlapping
// pattern hits collapse into the widest region. Pure and thread-safe: all
// pattern state is immutable after first use.
[[nodiscard]] std::vector<Finding> detect_secrets(const OcrResult& ocr,
                                                  const DetectorConfig& config = {});

// Collapses overlapping findings into the widest ones and sorts them in
// reading order. Used to merge detections from several OCR passes.
[[nodiscard]] std::vector<Finding> dedupe_findings(std::vector<Finding> findings);

// Validators exposed for tests and reuse.
[[nodiscard]] bool luhn_valid(std::string_view digits) noexcept;
[[nodiscard]] bool iban_valid(std::string_view candidate) noexcept;
[[nodiscard]] bool is_private_ipv4(std::string_view text) noexcept;
[[nodiscard]] double shannon_entropy_bits_per_char(std::string_view text) noexcept;

}  // namespace secretsimage
