#include "rtsync/sha256.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace rtsync {
namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

std::uint32_t rotr(std::uint32_t value, unsigned count) {
  return (value >> count) | (value << (32u - count));
}

}  // namespace

void Sha256::update(const void* data, std::size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  total_bytes_ += size;
  while (size > 0) {
    const std::size_t take = std::min(size, sizeof(block_) - block_size_);
    std::copy(bytes, bytes + take, block_ + block_size_);
    block_size_ += take;
    bytes += take;
    size -= take;
    if (block_size_ == sizeof(block_)) {
      compress(block_);
      block_size_ = 0;
    }
  }
}

std::string Sha256::finish() {
  const std::uint64_t bit_count = total_bytes_ * 8u;
  const unsigned char marker = 0x80;
  update(&marker, 1);
  const unsigned char zero = 0;
  while (block_size_ != 56) update(&zero, 1);
  unsigned char length[8];
  for (int i = 7; i >= 0; --i) {
    length[7 - i] = static_cast<unsigned char>((bit_count >> (i * 8)) & 0xffu);
  }
  update(length, sizeof(length));
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (std::uint32_t word : state_) out << std::setw(8) << word;
  return out.str();
}

void Sha256::compress(const unsigned char* block) {
  std::array<std::uint32_t, 64> schedule{};
  for (int i = 0; i < 16; ++i) {
    const std::size_t p = static_cast<std::size_t>(i) * 4;
    schedule[static_cast<std::size_t>(i)] =
        (std::uint32_t(block[p]) << 24u) | (std::uint32_t(block[p + 1]) << 16u) |
        (std::uint32_t(block[p + 2]) << 8u) | std::uint32_t(block[p + 3]);
  }
  for (std::size_t i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotr(schedule[i - 15], 7) ^
                             rotr(schedule[i - 15], 18) ^
                             (schedule[i - 15] >> 3u);
    const std::uint32_t s1 = rotr(schedule[i - 2], 17) ^
                             rotr(schedule[i - 2], 19) ^
                             (schedule[i - 2] >> 10u);
    schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
  }
  std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
  std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const std::uint32_t choose = (e & f) ^ ((~e) & g);
    const std::uint32_t temp1 =
        h + s1 + choose + kRoundConstants[i] + schedule[i];
    const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
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
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

std::string sha256_hex(const void* data, std::size_t size) {
  Sha256 hash;
  hash.update(data, size);
  return hash.finish();
}

std::string sha256_hex(const std::string& bytes) {
  return sha256_hex(bytes.data(), bytes.size());
}

std::string sha256_file_hex(const std::string& path, std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (error) *error = "cannot open " + path;
    return {};
  }
  Sha256 hash;
  std::vector<char> buffer(64 * 1024);
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = input.gcount();
    if (count > 0) hash.update(buffer.data(), static_cast<std::size_t>(count));
  }
  if (!input.eof()) {
    if (error) *error = "read failed for " + path;
    return {};
  }
  return hash.finish();
}

}  // namespace rtsync

