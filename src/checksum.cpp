#include "checksum.hpp"

#include <array>
#include <cstdio>
#include <cstring>

namespace rr {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

std::uint32_t rotr(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32U - bits));
}

std::string hexBytes(const std::uint8_t* data, std::size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.resize(size * 2U);
    for (std::size_t i = 0; i < size; ++i) {
        out[2U * i] = digits[data[i] >> 4U];
        out[2U * i + 1U] = digits[data[i] & 0x0FU];
    }
    return out;
}

}  // namespace

std::string sha256Hex(const std::uint8_t* data, std::size_t size) {
    std::array<std::uint32_t, 8> state{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::vector<std::uint8_t> padded(size);
    if (size > 0U) {
        std::memcpy(padded.data(), data, size);
    }
    padded.push_back(0x80U);
    while ((padded.size() % 64U) != 56U) {
        padded.push_back(0U);
    }
    const std::uint64_t bitLength = static_cast<std::uint64_t>(size) * 8U;
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xFFU));
    }

    for (std::size_t offset = 0; offset < padded.size(); offset += 64U) {
        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t i = 0; i < 16U; ++i) {
            const std::size_t base = offset + i * 4U;
            schedule[i] = (static_cast<std::uint32_t>(padded[base]) << 24U) |
                          (static_cast<std::uint32_t>(padded[base + 1U]) << 16U) |
                          (static_cast<std::uint32_t>(padded[base + 2U]) << 8U) |
                          static_cast<std::uint32_t>(padded[base + 3U]);
        }
        for (std::size_t i = 16U; i < 64U; ++i) {
            const std::uint32_t s0 = rotr(schedule[i - 15U], 7U) ^
                                     rotr(schedule[i - 15U], 18U) ^
                                     (schedule[i - 15U] >> 3U);
            const std::uint32_t s1 = rotr(schedule[i - 2U], 17U) ^
                                     rotr(schedule[i - 2U], 19U) ^
                                     (schedule[i - 2U] >> 10U);
            schedule[i] = schedule[i - 16U] + s0 + schedule[i - 7U] + s1;
        }

        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        std::uint32_t f = state[5];
        std::uint32_t g = state[6];
        std::uint32_t h = state[7];
        for (std::size_t i = 0; i < 64U; ++i) {
            const std::uint32_t s1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
            const std::uint32_t choose = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + s1 + choose + kRoundConstants[i] + schedule[i];
            const std::uint32_t s0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::array<std::uint8_t, 32> digest{};
    for (std::size_t i = 0; i < 8U; ++i) {
        digest[i * 4U] = static_cast<std::uint8_t>((state[i] >> 24U) & 0xFFU);
        digest[i * 4U + 1U] = static_cast<std::uint8_t>((state[i] >> 16U) & 0xFFU);
        digest[i * 4U + 2U] = static_cast<std::uint8_t>((state[i] >> 8U) & 0xFFU);
        digest[i * 4U + 3U] = static_cast<std::uint8_t>(state[i] & 0xFFU);
    }
    return hexBytes(digest.data(), digest.size());
}

std::string sha256Hex(std::string_view data) {
    return sha256Hex(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
}

std::string sha256Hex(const std::vector<std::uint8_t>& data) {
    return sha256Hex(data.data(), data.size());
}

std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string fnv1a64Hex(const std::vector<std::uint8_t>& data) {
    std::uint64_t value = fnv1a64(data.data(), data.size());
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(value));
    return std::string(buffer);
}

}  // namespace rr
