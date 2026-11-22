#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lshc {

struct RgbaImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

struct DiffStats {
    uint64_t totalPixels = 0;
    uint64_t changedPixels = 0;
    double changedRatio = 0.0;
    double meanAbs = 0.0;
    double rmse = 0.0;
    uint32_t maxAbs = 0;
    uint32_t minX = 0;
    uint32_t minY = 0;
    uint32_t maxX = 0;
    uint32_t maxY = 0;
    bool hasBounds = false;
};

void writePpm(const std::string& path, const RgbaImage& image);
RgbaImage readPpm(const std::string& path);
DiffStats compareImages(const RgbaImage& oldImage, const RgbaImage& newImage);
void writeDiffPpm(const std::string& path, const RgbaImage& oldImage,
                  const RgbaImage& newImage);

}  // namespace lshc
