#include "image.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace lshc {
namespace {

void validateImage(const RgbaImage& image)
{
    const uint64_t expected =
        static_cast<uint64_t>(image.width) * image.height * 4u;
    if (image.width == 0u || image.height == 0u ||
        image.pixels.size() != expected) {
        throw std::runtime_error("invalid RGBA image extent or byte count");
    }
}

uint32_t channelDifference(const RgbaImage& oldImage, const RgbaImage& newImage,
                           std::size_t pixel, unsigned channel)
{
    const int oldValue = oldImage.pixels[pixel * 4u + channel];
    const int newValue = newImage.pixels[pixel * 4u + channel];
    return static_cast<uint32_t>(std::abs(oldValue - newValue));
}

}  // namespace

void writePpm(const std::string& path, const RgbaImage& image)
{
    validateImage(image);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write image: " + path);
    }
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (std::size_t pixel = 0;
         pixel < static_cast<std::size_t>(image.width) * image.height; ++pixel) {
        output.write(reinterpret_cast<const char*>(&image.pixels[pixel * 4u]), 3);
    }
    if (!output) {
        throw std::runtime_error("failed writing image: " + path);
    }
}

RgbaImage readPpm(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read image: " + path);
    }
    std::string magic;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t maxValue = 0;
    input >> magic >> width >> height >> maxValue;
    if (!input || magic != "P6" || width == 0u || height == 0u ||
        maxValue != 255u) {
        throw std::runtime_error("unsupported PPM image: " + path);
    }
    input.get();
    RgbaImage image;
    image.width = width;
    image.height = height;
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    image.pixels.resize(pixelCount * 4u);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        input.read(reinterpret_cast<char*>(&image.pixels[pixel * 4u]), 3);
        image.pixels[pixel * 4u + 3u] = 255u;
    }
    if (!input) {
        throw std::runtime_error("truncated PPM image: " + path);
    }
    return image;
}

DiffStats compareImages(const RgbaImage& oldImage, const RgbaImage& newImage)
{
    validateImage(oldImage);
    validateImage(newImage);
    if (oldImage.width != newImage.width ||
        oldImage.height != newImage.height) {
        throw std::runtime_error("cannot compare images with different extents");
    }

    DiffStats stats;
    stats.totalPixels =
        static_cast<uint64_t>(oldImage.width) * oldImage.height;
    stats.minX = oldImage.width;
    stats.minY = oldImage.height;
    double sumAbs = 0.0;
    double sumSquared = 0.0;
    uint64_t comparedChannels = 0;

    for (std::size_t pixel = 0;
         pixel < static_cast<std::size_t>(stats.totalPixels); ++pixel) {
        uint32_t pixelMax = 0;
        for (unsigned channel = 0; channel < 3u; ++channel) {
            const uint32_t difference =
                channelDifference(oldImage, newImage, pixel, channel);
            pixelMax = std::max(pixelMax, difference);
            stats.maxAbs = std::max(stats.maxAbs, difference);
            sumAbs += difference;
            sumSquared += static_cast<double>(difference) * difference;
            ++comparedChannels;
        }
        if (pixelMax == 0u) {
            continue;
        }
        ++stats.changedPixels;
        const uint32_t x = static_cast<uint32_t>(
            pixel % static_cast<std::size_t>(oldImage.width));
        const uint32_t y = static_cast<uint32_t>(
            pixel / static_cast<std::size_t>(oldImage.width));
        stats.minX = std::min(stats.minX, x);
        stats.minY = std::min(stats.minY, y);
        stats.maxX = std::max(stats.maxX, x);
        stats.maxY = std::max(stats.maxY, y);
        stats.hasBounds = true;
    }

    if (stats.totalPixels != 0u) {
        stats.changedRatio = static_cast<double>(stats.changedPixels) /
                             static_cast<double>(stats.totalPixels);
    }
    if (comparedChannels != 0u) {
        stats.meanAbs = sumAbs / static_cast<double>(comparedChannels);
        stats.rmse = std::sqrt(sumSquared / static_cast<double>(comparedChannels));
    }
    return stats;
}

void writeDiffPpm(const std::string& path, const RgbaImage& oldImage,
                  const RgbaImage& newImage)
{
    validateImage(oldImage);
    validateImage(newImage);
    if (oldImage.width != newImage.width ||
        oldImage.height != newImage.height) {
        throw std::runtime_error("cannot diff images with different extents");
    }
    RgbaImage difference;
    difference.width = oldImage.width;
    difference.height = oldImage.height;
    difference.pixels.assign(oldImage.pixels.size(), 255u);
    for (std::size_t pixel = 0;
         pixel < static_cast<std::size_t>(difference.width) *
                     difference.height;
         ++pixel) {
        const uint32_t red = channelDifference(oldImage, newImage, pixel, 0u);
        const uint32_t green = channelDifference(oldImage, newImage, pixel, 1u);
        const uint32_t blue = channelDifference(oldImage, newImage, pixel, 2u);
        const uint32_t luminance = (red * 3u + green * 6u + blue) / 10u;
        const auto boosted =
            static_cast<uint8_t>(std::min<uint32_t>(255u, luminance * 6u));
        difference.pixels[pixel * 4u] = boosted;
        difference.pixels[pixel * 4u + 1u] =
            static_cast<uint8_t>(std::min<uint32_t>(255u, green * 2u));
        difference.pixels[pixel * 4u + 2u] =
            static_cast<uint8_t>(std::min<uint32_t>(255u, blue * 3u));
    }
    writePpm(path, difference);
}

}  // namespace lshc
