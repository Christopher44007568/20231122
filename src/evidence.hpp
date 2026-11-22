#pragma once

#include "common.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace inst {

struct ImageDelta {
    uint64_t differingPixels = 0;
    uint64_t totalPixels = 0;
    double meanAbsError = 0.0;
    uint32_t maxChannelError = 0;
    double differingRatio = 0.0;
};

ImageDelta compareImages(const std::vector<uint8_t>& a,
                         const std::vector<uint8_t>& b,
                         uint32_t width, uint32_t height);

void writePpm(const fs::path& path, const std::vector<uint8_t>& rgba,
              uint32_t width, uint32_t height);

void writeDiffPpm(const fs::path& path,
                  const std::vector<uint8_t>& legacy,
                  const std::vector<uint8_t>& stable,
                  uint32_t width, uint32_t height);

std::string imageHash(const std::vector<uint8_t>& rgba);

} // namespace inst
