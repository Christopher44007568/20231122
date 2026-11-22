#include "sha256.h"

#include <array>
#include <iomanip>
#include <sstream>

namespace hdrprov {
namespace {

constexpr std::array<std::uint32_t, 64> kRound = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t rotr(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

class Sha256 {
public:
    Sha256()
        : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

    void update(const std::uint8_t* data, std::size_t size) {
        for (std::size_t index = 0; index < size; ++index) {
            buffer_[bufferSize_++] = data[index];
            if (bufferSize_ == buffer_.size()) {
                transform(buffer_.data());
                bitCount_ += 512;
                bufferSize_ = 0;
            }
        }
    }

    std::string finish() {
        const std::uint64_t totalBits = bitCount_ + bufferSize_ * 8;
        buffer_[bufferSize_++] = 0x80;
        if (bufferSize_ > 56) {
            while (bufferSize_ < 64) buffer_[bufferSize_++] = 0;
            transform(buffer_.data());
            bufferSize_ = 0;
        }
        while (bufferSize_ < 56) buffer_[bufferSize_++] = 0;
        for (int shift = 7; shift >= 0; --shift) {
            buffer_[bufferSize_++] =
                static_cast<std::uint8_t>((totalBits >> (shift * 8)) & 0xff);
        }
        transform(buffer_.data());
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (const auto word : state_) output << std::setw(8) << word;
        return output.str();
    }

private:
    void transform(const std::uint8_t* block) {
        std::uint32_t words[64]{};
        for (int index = 0; index < 16; ++index) {
            words[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24) |
                           (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16) |
                           (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8) |
                           static_cast<std::uint32_t>(block[index * 4 + 3]);
        }
        for (int index = 16; index < 64; ++index) {
            const auto s0 = rotr(words[index - 15], 7) ^
                            rotr(words[index - 15], 18) ^ (words[index - 15] >> 3);
            const auto s1 = rotr(words[index - 2], 17) ^
                            rotr(words[index - 2], 19) ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int index = 0; index < 64; ++index) {
            const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const auto choose = (e & f) ^ ((~e) & g);
            const auto temp1 = h + s1 + choose + kRound[index] + words[index];
            const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = s0 + majority;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_;
    std::uint64_t bitCount_ = 0;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t bufferSize_ = 0;
};

}  // namespace

std::string sha256Hex(const std::uint8_t* data, std::size_t size) {
    Sha256 hash;
    hash.update(data, size);
    return hash.finish();
}

std::string sha256Hex(const std::vector<std::uint8_t>& data) {
    return sha256Hex(data.data(), data.size());
}

std::string sha256Hex(const std::string& text) {
    return sha256Hex(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

}  // namespace hdrprov
