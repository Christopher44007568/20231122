#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tar {

struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;  // width * height * 4, top-down
};

// Writes an 8-bit RGBA PNG using zlib "stored" (uncompressed) deflate
// blocks and filter type 0, so no zlib dependency is required.
void WritePng(const std::string& path, const Image& img);

// Reads back PNGs produced by WritePng (8-bit RGBA/RGB, stored deflate
// blocks, filter 0). Throws PipelineError on anything else.
Image ReadPng(const std::string& path);

}  // namespace tar
