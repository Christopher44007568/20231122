#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

class Sha256 {
public:
  Sha256();
  void update(const void* data, size_t len);
  // Returns lowercase hex digest (64 chars).
  std::string finalHex();

private:
  uint32_t state_[8];
  uint64_t bitLen_ = 0;
  uint8_t buf_[64];
  size_t bufLen_ = 0;
  void compress(const uint8_t* block);
};

std::string sha256Hex(const void* data, size_t len);
std::string sha256FileHex(const std::string& path);
