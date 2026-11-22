#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rr {

struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;

    bool valid() const;
    std::size_t pixelCount() const;
    std::array<std::uint8_t, 4> pixel(std::uint32_t x, std::uint32_t y) const;
};

struct DiffBox {
    bool empty = true;
    std::uint32_t minX = 0;
    std::uint32_t minY = 0;
    std::uint32_t maxX = 0;
    std::uint32_t maxY = 0;
};

struct DiffStats {
    bool comparable = false;
    std::uint64_t changedPixels = 0;
    std::uint64_t totalPixels = 0;
    std::uint32_t maxChannelDelta = 0;
    double meanAbsoluteError = 0.0;
    DiffBox box;
    bool hasFirstDiff = false;
    std::uint32_t firstDiffX = 0;
    std::uint32_t firstDiffY = 0;
};

struct ReferenceCheck {
    std::string pass;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::array<std::uint8_t, 4> expected{0U, 0U, 0U, 0U};
    std::array<std::uint8_t, 4> actual{0U, 0U, 0U, 0U};
    std::uint32_t tolerance = 0;
    std::uint32_t maxDelta = 0;
    bool passCheck = false;
};

DiffStats compareImages(const Image& before, const Image& after, std::uint32_t threshold = 0U);
Image makeDiffImage(const Image& before, const Image& after, std::uint32_t gain = 8U);
ReferenceCheck checkReference(const Image& image,
                              std::uint32_t x,
                              std::uint32_t y,
                              const std::array<std::uint8_t, 4>& expected,
                              std::uint32_t tolerance);

bool writePpm(const std::string& path, const Image& image);
bool writePam(const std::string& path, const Image& image);
bool writeDiffPpm(const std::string& path, const Image& before, const Image& after);

}  // namespace rr
