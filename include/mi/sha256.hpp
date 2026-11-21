#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mi {

class Sha256 {
 public:
  Sha256();
  void update(const void* data, std::size_t size);
  void update(const std::string& text);
  std::string finish_hex();

 private:
  void transform(const std::uint8_t* block);

  std::uint32_t state_[8];
  std::uint64_t bit_count_;
  std::uint8_t buffer_[64];
  std::size_t buffer_size_;
  bool finished_;
};

std::string sha256_hex(const std::string& text);

}  // namespace mi
