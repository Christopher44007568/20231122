#include "capture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>

namespace inspect {
namespace {

float distanceToSegment(float px, float py, float x0, float y0, float x1,
                        float y1) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length_squared = dx * dx + dy * dy;
    if (length_squared <= 1.0e-12f) {
        return std::hypot(px - x0, py - y0);
    }
    float t = ((px - x0) * dx + (py - y0) * dy) / length_squared;
    t = std::clamp(t, 0.0f, 1.0f);
    return std::hypot(px - (x0 + t * dx), py - (y0 + t * dy));
}

std::uint8_t toByte(float value) {
    return static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255));
}

}  // namespace

std::vector<std::uint8_t> rasterizeRecords(const std::vector<GsRecord>& records,
                                           std::uint32_t width,
                                           std::uint32_t height) {
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(width) * height * 4U);
    for (std::size_t pixel = 0; pixel < rgba.size(); pixel += 4U) {
        rgba[pixel] = 8U;
        rgba[pixel + 1] = 12U;
        rgba[pixel + 2] = 20U;
        rgba[pixel + 3] = 255U;
    }

    const float pixel_radius = 1.35f;
    for (const GsRecord& record : records) {
        const float x0 = (record.p0[0] * 0.5f + 0.5f) * width;
        const float y0 = (0.5f - record.p0[1] * 0.5f) * height;
        const float x1 = (record.p1[0] * 0.5f + 0.5f) * width;
        const float y1 = (0.5f - record.p1[1] * 0.5f) * height;
        const int min_x = std::max(
            0, static_cast<int>(std::floor(std::min(x0, x1) - pixel_radius)));
        const int max_x = std::min(
            static_cast<int>(width) - 1,
            static_cast<int>(std::ceil(std::max(x0, x1) + pixel_radius)));
        const int min_y = std::max(
            0, static_cast<int>(std::floor(std::min(y0, y1) - pixel_radius)));
        const int max_y = std::min(
            static_cast<int>(height) - 1,
            static_cast<int>(std::ceil(std::max(y0, y1) + pixel_radius)));

        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                const float distance = distanceToSegment(
                    static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f,
                    x0, y0, x1, y1);
                if (distance > pixel_radius) {
                    continue;
                }
                const float coverage =
                    std::clamp(pixel_radius - distance, 0.0f, 1.0f);
                const std::size_t index =
                    (static_cast<std::size_t>(y) * width +
                     static_cast<std::size_t>(x)) *
                    4U;
                for (std::uint32_t channel = 0; channel < 3; ++channel) {
                    const float background =
                        static_cast<float>(rgba[index + channel]) / 255.0f;
                    const float foreground = std::clamp(
                        record.color[channel] * (0.55f + coverage * 0.45f),
                        0.0f, 1.0f);
                    rgba[index + channel] =
                        toByte(background + (foreground - background) * coverage);
                }
            }
        }
    }
    return rgba;
}

bool writePpm(const std::filesystem::path& path,
              const std::vector<std::uint8_t>& rgba, std::uint32_t width,
              std::uint32_t height) {
    if (rgba.size() != static_cast<std::size_t>(width) * height * 4U) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (std::size_t index = 0; index < rgba.size(); index += 4U) {
        output.put(static_cast<char>(rgba[index]));
        output.put(static_cast<char>(rgba[index + 1U]));
        output.put(static_cast<char>(rgba[index + 2U]));
    }
    return output.good();
}

}  // namespace inspect
