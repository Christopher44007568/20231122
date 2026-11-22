#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Writes an 8-bit RGBA image as PNG (zlib "stored" deflate blocks, no
// external compression dependency). Returns false on I/O failure.
bool writePngRgba(const std::string& path, uint32_t width, uint32_t height,
                  const std::vector<uint8_t>& rgba);
