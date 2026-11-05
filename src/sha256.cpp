#include "secretsimage/sha256.hpp"

#include <cstring>

namespace secretsimage {
namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

constexpr std::uint32_t rotr(std::uint32_t value, int bits) noexcept {
    return (value >> bits) | (value << (32 - bits));
}

}  // namespace

Sha256::Sha256()
    : state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
             0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U} {}

void Sha256::process_block(const std::uint8_t* block) noexcept {
    std::uint32_t w[64]{};
    for (int i = 0; i < 16; ++i) {
        const auto o = static_cast<std::size_t>(i) * 4U;
        w[i] = (static_cast<std::uint32_t>(block[o]) << 24U) |
               (static_cast<std::uint32_t>(block[o + 1]) << 16U) |
               (static_cast<std::uint32_t>(block[o + 2]) << 8U) |
               static_cast<std::uint32_t>(block[o + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        const std::uint32_t s0 = rotr(w[idx - 15], 7) ^ rotr(w[idx - 15], 18) ^ (w[idx - 15] >> 3U);
        const std::uint32_t s1 = rotr(w[idx - 2], 17) ^ rotr(w[idx - 2], 19) ^ (w[idx - 2] >> 10U);
        w[idx] = w[idx - 16] + s0 + w[idx - 7] + s1;
    }
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        const std::uint32_t big_s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + big_s1 + ch + kRoundConstants[idx] + w[idx];
        const std::uint32_t big_s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = big_s0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(std::span<const std::uint8_t> data) noexcept {
    total_bytes_ += data.size();
    while (!data.empty()) {
        if (buffer_size_ == 0 && data.size() >= buffer_.size()) {
            process_block(data.data());
            data = data.subspan(buffer_.size());
            continue;
        }
        const std::size_t take = std::min(buffer_.size() - buffer_size_, data.size());
        std::memcpy(buffer_.data() + buffer_size_, data.data(), take);
        buffer_size_ += take;
        data = data.subspan(take);
        if (buffer_size_ == buffer_.size()) {
            process_block(buffer_.data());
            buffer_size_ = 0;
        }
    }
}

std::array<std::uint8_t, 32> Sha256::final() noexcept {
    const std::uint64_t bit_length = total_bytes_ * 8U;
    const std::uint8_t pad_start = 0x80U;
    update(std::span<const std::uint8_t>{&pad_start, 1});
    const std::uint8_t zero = 0;
    while (buffer_size_ != 56) {
        update(std::span<const std::uint8_t>{&zero, 1});
    }
    std::array<std::uint8_t, 8> length_bytes{};
    for (int i = 0; i < 8; ++i) {
        length_bytes[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(bit_length >> (56 - 8 * i));
    }
    // Feed the length without disturbing the byte counter bookkeeping:
    // process it as the tail of the final block.
    for (std::uint8_t byte : length_bytes) {
        buffer_[buffer_size_++] = byte;
        if (buffer_size_ == buffer_.size()) {
            process_block(buffer_.data());
            buffer_size_ = 0;
        }
    }
    std::array<std::uint8_t, 32> digest{};
    for (int i = 0; i < 8; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        digest[idx * 4] = static_cast<std::uint8_t>(state_[idx] >> 24U);
        digest[idx * 4 + 1] = static_cast<std::uint8_t>(state_[idx] >> 16U);
        digest[idx * 4 + 2] = static_cast<std::uint8_t>(state_[idx] >> 8U);
        digest[idx * 4 + 3] = static_cast<std::uint8_t>(state_[idx]);
    }
    return digest;
}

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) noexcept {
    Sha256 hasher;
    hasher.update(data);
    return hasher.final();
}

std::string to_hex(std::span<const std::uint8_t> data) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.resize(data.size() * 2U);
    for (std::size_t i = 0; i < data.size(); ++i) {
        out[2U * i] = kDigits[data[i] >> 4U];
        out[2U * i + 1U] = kDigits[data[i] & 0x0FU];
    }
    return out;
}

std::string sha256_hex(std::span<const std::uint8_t> data) {
    const auto digest = sha256(data);
    return to_hex(digest);
}

}  // namespace secretsimage
