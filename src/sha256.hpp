#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

namespace nmts {

class Sha256 {
 public:
  Sha256();
  void update(const std::uint8_t* data, std::size_t len);
  void update(std::string_view text);
  std::array<std::uint8_t, 32> finish();

 private:
  void compress(const std::uint8_t* block);
  std::uint32_t state_[8]{};
  std::uint64_t bit_len_ = 0;
  std::uint8_t buffer_[64]{};
  std::size_t buffer_len_ = 0;
  bool finished_ = false;
};

std::string sha256_hex_file(const std::string& path);
std::string sha256_hex(const std::uint8_t* data, std::size_t len);

}  // namespace nmts
