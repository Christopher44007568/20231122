#pragma once
#include <cstdint>
#include <string>

namespace pcd {

// Minimal self-contained SHA-256 (FIPS 180-4).
class Sha256 {
public:
  Sha256();
  void update(const uint8_t* data, size_t len);
  void final(uint8_t out[32]);
  static std::string file_hex(const std::string& path);
  static std::string bytes_hex(const void* data, size_t len);

private:
  void block(const uint8_t* p);
  uint32_t h_[8];
  uint8_t buf_[64];
  size_t buf_len_ = 0;
  uint64_t total_ = 0;
};

} // namespace pcd
