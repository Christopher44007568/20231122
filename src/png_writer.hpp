#pragma once

#include "common.hpp"

#include <cstdint>
#include <filesystem>

namespace offscreen {

// pixels is tightly packed RGBA8, top row first.
void writeRgbaPng(const std::filesystem::path& path, std::uint32_t width,
                  std::uint32_t height, const Bytes& pixels);

}  // namespace offscreen
