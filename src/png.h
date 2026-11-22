#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Image {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgba;  // width*height*4
};

// Writes 8-bit RGBA PNG using uncompressed deflate blocks (no zlib needed).
bool writePng(const std::string& path, const Image& img);

// Absolute-difference heatmap: identical -> black, differences -> red ramp.
Image diffHeatmap(const Image& a, const Image& b,
                  uint64_t* diffPixelCount, uint32_t* maxChannelDiff);
