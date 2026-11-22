#include "evidence.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace inst {

ImageDelta compareImages(const std::vector<uint8_t>& a,
                         const std::vector<uint8_t>& b,
                         uint32_t width, uint32_t height) {
    ImageDelta delta;
    delta.totalPixels = static_cast<uint64_t>(width) * height;
    if (a.size() != b.size() || a.size() < delta.totalPixels * 4) {
        delta.differingPixels = delta.totalPixels;
        delta.maxChannelError = 255;
        delta.meanAbsError = 255.0;
        delta.differingRatio = 1.0;
        return delta;
    }
    uint64_t errorSum = 0;
    for (uint64_t pixel = 0; pixel < delta.totalPixels; ++pixel) {
        bool different = false;
        for (uint32_t channel = 0; channel < 4; ++channel) {
            const size_t offset = static_cast<size_t>(pixel * 4 + channel);
            const uint32_t difference =
                static_cast<uint32_t>(std::abs(static_cast<int>(a[offset]) -
                                               static_cast<int>(b[offset])));
            errorSum += difference;
            delta.maxChannelError = std::max(delta.maxChannelError, difference);
            if (difference != 0) different = true;
        }
        if (different) ++delta.differingPixels;
    }
    delta.meanAbsError = static_cast<double>(errorSum) /
                         static_cast<double>(delta.totalPixels * 4);
    delta.differingRatio = static_cast<double>(delta.differingPixels) /
                           static_cast<double>(delta.totalPixels);
    return delta;
}

void writePpm(const fs::path& path, const std::vector<uint8_t>& rgba,
              uint32_t width, uint32_t height) {
    const size_t expected = static_cast<size_t>(width) * height * 4;
    if (rgba.size() < expected) {
        throw std::runtime_error("PPM source has too few pixels");
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << "P6\n" << width << ' ' << height << "\n255\n";
    std::vector<uint8_t> row(static_cast<size_t>(width) * 3);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t source =
                (static_cast<size_t>(y) * width + x) * 4;
            const size_t target = static_cast<size_t>(x) * 3;
            row[target + 0] = rgba[source + 0];
            row[target + 1] = rgba[source + 1];
            row[target + 2] = rgba[source + 2];
        }
        out.write(reinterpret_cast<const char*>(row.data()),
                  static_cast<std::streamsize>(row.size()));
    }
}

void writeDiffPpm(const fs::path& path,
                  const std::vector<uint8_t>& legacy,
                  const std::vector<uint8_t>& stable,
                  uint32_t width, uint32_t height) {
    const size_t pixels = static_cast<size_t>(width) * height;
    if (legacy.size() < pixels * 4 || stable.size() < pixels * 4) {
        throw std::runtime_error("diff source has too few pixels");
    }
    const uint32_t panelWidth = width;
    const uint32_t outputWidth = width * 3;
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << "P6\n" << outputWidth << ' ' << height << "\n255\n";
    std::vector<uint8_t> row(static_cast<size_t>(outputWidth) * 3);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t source = static_cast<size_t>(y) * width + x;
            for (uint32_t panel = 0; panel < 3; ++panel) {
                const size_t target =
                    (static_cast<size_t>(panel) * panelWidth + x) * 3;
                if (panel < 2) {
                    const auto& image = panel == 0 ? legacy : stable;
                    row[target + 0] = image[source * 4 + 0];
                    row[target + 1] = image[source * 4 + 1];
                    row[target + 2] = image[source * 4 + 2];
                } else {
                    for (uint32_t channel = 0; channel < 3; ++channel) {
                        const uint32_t difference = static_cast<uint32_t>(
                            std::abs(static_cast<int>(legacy[source * 4 + channel]) -
                                     static_cast<int>(stable[source * 4 + channel])));
                        row[target + channel] = static_cast<uint8_t>(
                            std::min<uint32_t>(255u, difference * 8u));
                    }
                }
            }
        }
        out.write(reinterpret_cast<const char*>(row.data()),
                  static_cast<std::streamsize>(row.size()));
    }
}

std::string imageHash(const std::vector<uint8_t>& rgba) {
    return hex64(fnv1a64(rgba.data(), rgba.size()));
}

} // namespace inst
