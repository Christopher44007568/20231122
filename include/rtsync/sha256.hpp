#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rtsync {

class Sha256 {
 public:
  void update(const void* data, std::size_t size);
  std::string finish();

 private:
  std::uint32_t state_[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                             0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                             0x1f83d9abu, 0x5be0cd19u};
  unsigned char block_[64] = {};
  std::size_t block_size_ = 0;
  std::uint64_t total_bytes_ = 0;
  void compress(const unsigned char* block);
};

std::string sha256_hex(const void* data, std::size_t size);
std::string sha256_hex(const std::string& bytes);
std::string sha256_file_hex(const std::string& path, std::string* error = nullptr);

}  // namespace rtsync

