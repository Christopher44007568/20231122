#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace tcpv {

struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba; // 4 bytes per pixel, row-major, top-left origin

  Image() = default;
  Image(uint32_t w, uint32_t h) : width(w), height(h), rgba(size_t(w) * h * 4, 0) {}

  uint8_t* px(uint32_t x, uint32_t y) { return &rgba[(size_t(y) * width + x) * 4]; }
  const uint8_t* px(uint32_t x, uint32_t y) const {
    return &rgba[(size_t(y) * width + x) * 4];
  }
};

bool writePng(const std::string& path, const Image& img);

} // namespace tcpv
