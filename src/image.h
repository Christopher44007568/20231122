#pragma once

#include <cstdint>
#include <string>
#include <vector>

// RGBA8 image helpers: PPM dump for evidence and pixel-diff for diagnosis.
struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;  // 4 bytes per pixel
};

struct DiffResult {
  uint64_t differingPixels = 0;
  uint64_t maxChannelDelta = 0;
  // Bounding box of differing pixels (valid only when differingPixels > 0).
  uint32_t minX = 0, minY = 0, maxX = 0, maxY = 0;
  Image diffImage;  // red where pixels differ, dimmed original elsewhere
};

void savePpm(const Image& img, const std::string& path);
DiffResult diffImages(const Image& a, const Image& b, uint8_t tolerance);
