#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rpsc {

struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;  // 4 bytes per pixel
  uint64_t hash() const;      // FNV-1a over pixels
};

bool savePpm(const std::string& path, const Image& img, std::string* err);
bool loadPpm(const std::string& path, Image* img, std::string* err);

struct DiffStats {
  uint64_t diffPixels = 0;
  uint32_t maxDelta = 0;
  double meanDelta = 0.0;
  bool sizeMismatch = false;
};

// Per-pixel absolute difference; `diffOut` (optional) gets an amplified
// visualization (delta x4, clamped) for evidence images.
DiffStats diffImages(const Image& a, const Image& b, Image* diffOut);

}  // namespace rpsc
