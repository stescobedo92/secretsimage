#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace secretsimage {

// Minimal, dependency-free SHA-256 (FIPS 180-4). Used to fingerprint images
// in the audit log and to recognize our own clipboard writes so the watcher
// never redacts its own output in a loop. Not a general crypto library.
class Sha256 {
  public:
    Sha256();
    void update(std::span<const std::uint8_t> data) noexcept;
    [[nodiscard]] std::array<std::uint8_t, 32> final() noexcept;

  private:
    void process_block(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_size_ = 0;
    std::uint64_t total_bytes_ = 0;
};

[[nodiscard]] std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) noexcept;
[[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> data);
[[nodiscard]] std::string to_hex(std::span<const std::uint8_t> data);

}  // namespace secretsimage
