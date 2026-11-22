#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "camtrak/types.hpp"

namespace camtrak {

// Streaming SHA-256, FIPS 180-4. Implemented locally so evidence hashing has
// no dependency on OpenSSL or any other platform library.
class Sha256 {
 public:
  Sha256();
  void update(const std::uint8_t* data, std::size_t length);
  void update(const std::string& data);
  // Returns the 32-byte digest; the hasher may continue to be used afterwards.
  Bytes digest() const;
  void reset();

 private:
  void compress(const std::uint8_t* block);
  // Feeds bytes into the 64-byte block buffer without touching bit_length_.
  void compressPendingBytes(const std::uint8_t* data, std::size_t length);

  std::uint32_t state_[8];
  std::uint64_t bit_length_ = 0;
  std::uint8_t buffer_[64];
  std::size_t buffer_len_ = 0;
};

// Convenience one-shot helper returning lowercase hex.
std::string sha256Hex(const std::string& data);
std::string sha256Hex(const Bytes& data);
std::string toHex(const Bytes& data);

}  // namespace camtrak
