#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace kfc {

class Sha256 {
public:
  Sha256();

  void update(const void* data, std::size_t length);
  void update(std::string_view data) { update(data.data(), data.size()); }
  std::array<std::uint8_t, 32> finish();

  static std::array<std::uint8_t, 32> digest(std::string_view data);
  static std::string hex(const std::array<std::uint8_t, 32>& bytes);
  static std::string of(std::string_view data) { return hex(digest(data)); }

private:
  std::array<std::uint32_t, 8> state_;
  std::array<std::uint8_t, 64> block_{};
  std::uint64_t totalBytes_ = 0;
  std::size_t blockBytes_ = 0;
  bool finished_ = false;

  void transform(const std::uint8_t* block);
};

}  // namespace kfc
