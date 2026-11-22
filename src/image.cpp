#include "image.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>

namespace rr {

bool Image::valid() const {
    return width > 0U && height > 0U &&
           rgba.size() == static_cast<std::size_t>(width) * height * 4U;
}

std::size_t Image::pixelCount() const {
    return static_cast<std::size_t>(width) * height;
}

std::array<std::uint8_t, 4> Image::pixel(std::uint32_t x, std::uint32_t y) const {
    if (x >= width || y >= height) {
        return {0U, 0U, 0U, 0U};
    }
    const std::size_t offset =
        (static_cast<std::size_t>(y) * width + x) * 4U;
    return {rgba[offset], rgba[offset + 1U], rgba[offset + 2U], rgba[offset + 3U]};
}

DiffStats compareImages(const Image& before, const Image& after, std::uint32_t threshold) {
    DiffStats stats;
    if (!before.valid() || !after.valid() || before.width != after.width ||
        before.height != after.height) {
        return stats;
    }
    stats.comparable = true;
    stats.totalPixels = before.pixelCount();
    double sum = 0.0;
    for (std::uint32_t y = 0; y < before.height; ++y) {
        for (std::uint32_t x = 0; x < before.width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * before.width + x) * 4U;
            std::uint32_t maxDelta = 0;
            for (std::size_t channel = 0; channel < 4U; ++channel) {
                const int delta = std::abs(static_cast<int>(before.rgba[offset + channel]) -
                                           static_cast<int>(after.rgba[offset + channel]));
                sum += static_cast<double>(delta);
                maxDelta = std::max(maxDelta, static_cast<std::uint32_t>(delta));
            }
            stats.maxChannelDelta = std::max(stats.maxChannelDelta, maxDelta);
            if (maxDelta > threshold) {
                ++stats.changedPixels;
                if (!stats.hasFirstDiff) {
                    stats.hasFirstDiff = true;
                    stats.firstDiffX = x;
                    stats.firstDiffY = y;
                }
                if (stats.box.empty) {
                    stats.box.empty = false;
                    stats.box.minX = x;
                    stats.box.minY = y;
                    stats.box.maxX = x;
                    stats.box.maxY = y;
                } else {
                    stats.box.minX = std::min(stats.box.minX, x);
                    stats.box.minY = std::min(stats.box.minY, y);
                    stats.box.maxX = std::max(stats.box.maxX, x);
                    stats.box.maxY = std::max(stats.box.maxY, y);
                }
            }
        }
    }
    const double samples =
        std::max(1.0, static_cast<double>(before.pixelCount()) * 4.0);
    stats.meanAbsoluteError = sum / samples;
    return stats;
}

Image makeDiffImage(const Image& before, const Image& after, std::uint32_t gain) {
    Image diff;
    if (!before.valid() || !after.valid() || before.width != after.width ||
        before.height != after.height) {
        return diff;
    }
    diff.width = before.width;
    diff.height = before.height;
    diff.rgba.resize(before.rgba.size());
    for (std::size_t i = 0; i < before.rgba.size(); i += 4U) {
        bool changed = false;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            const std::uint32_t delta = static_cast<std::uint32_t>(
                std::abs(static_cast<int>(before.rgba[i + channel]) -
                         static_cast<int>(after.rgba[i + channel])));
            const std::uint32_t amplified = std::min<std::uint32_t>(255U, delta * gain);
            diff.rgba[i + channel] = static_cast<std::uint8_t>(amplified);
            changed = changed || delta != 0U;
        }
        const std::uint32_t alphaDelta = static_cast<std::uint32_t>(
            std::abs(static_cast<int>(before.rgba[i + 3U]) -
                     static_cast<int>(after.rgba[i + 3U])));
        diff.rgba[i + 3U] = 255U;
        if (changed && alphaDelta == 0U) {
            diff.rgba[i] = static_cast<std::uint8_t>(
                std::min(255, static_cast<int>(diff.rgba[i]) + 32));
        }
    }
    return diff;
}

ReferenceCheck checkReference(const Image& image,
                              std::uint32_t x,
                              std::uint32_t y,
                              const std::array<std::uint8_t, 4>& expected,
                              std::uint32_t tolerance) {
    ReferenceCheck check;
    check.x = x;
    check.y = y;
    check.expected = expected;
    check.tolerance = tolerance;
    check.actual = image.pixel(x, y);
    for (std::size_t channel = 0; channel < 4U; ++channel) {
        const std::uint32_t delta = static_cast<std::uint32_t>(
            std::abs(static_cast<int>(expected[channel]) -
                     static_cast<int>(check.actual[channel])));
        check.maxDelta = std::max(check.maxDelta, delta);
    }
    check.passCheck = check.maxDelta <= tolerance;
    return check;
}

bool writePpm(const std::string& path, const Image& image) {
    if (!image.valid()) {
        return false;
    }
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (std::size_t i = 0; i < image.rgba.size(); i += 4U) {
        stream.put(static_cast<char>(image.rgba[i]));
        stream.put(static_cast<char>(image.rgba[i + 1U]));
        stream.put(static_cast<char>(image.rgba[i + 2U]));
    }
    return static_cast<bool>(stream);
}

bool writePam(const std::string& path, const Image& image) {
    if (!image.valid()) {
        return false;
    }
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream << "P7\nWIDTH " << image.width << "\nHEIGHT " << image.height
           << "\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
    stream.write(reinterpret_cast<const char*>(image.rgba.data()),
                 static_cast<std::streamsize>(image.rgba.size()));
    return static_cast<bool>(stream);
}

bool writeDiffPpm(const std::string& path, const Image& before, const Image& after) {
    return writePpm(path, makeDiffImage(before, after));
}

}  // namespace rr
