#include "checkpoint.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {

CheckpointHeader makeHeader(uint64_t nextFrame, const MemoryPlan& plan,
                            size_t halfPixelCount)
{
    CheckpointHeader header;
    header.headerBytes = sizeof(CheckpointHeader);
    header.nextFrame = nextFrame;
    header.width = static_cast<uint32_t>(plan.width);
    header.height = static_cast<uint32_t>(plan.height);
    header.tier = static_cast<uint32_t>(plan.tier);
    header.steps = static_cast<uint32_t>(plan.steps);
    header.imageBytes = static_cast<uint64_t>(halfPixelCount) * sizeof(uint16_t);
    return header;
}

float luminanceOf(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

double safePsnr(float rmse)
{
    if (rmse <= 1.0e-9f) {
        return std::numeric_limits<double>::infinity();
    }
    return 20.0 * std::log10(1.0 / static_cast<double>(rmse));
}

} // namespace

uint64_t fnv1a64(const void* data, size_t bytes, uint64_t seed)
{
    const auto* input = static_cast<const uint8_t*>(data);
    uint64_t hash = seed;
    for (size_t index = 0; index < bytes; ++index) {
        hash ^= input[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t fnv1a64(const std::vector<uint16_t>& values)
{
    return fnv1a64(values.data(), values.size() * sizeof(uint16_t));
}

uint16_t floatToHalf(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xffu) - 127 + 15;
    const uint32_t mantissa = bits & 0x7fffffu;

    if (exponent <= 0) {
        if (exponent < -10) {
            return static_cast<uint16_t>(sign);
        }
        const uint32_t normalized = (mantissa | 0x800000u) >> (1 - exponent);
        const uint32_t rounded = normalized + 0x1000u +
                                 ((normalized & 0x2000u) ? 1u : 0u);
        return static_cast<uint16_t>(sign | (rounded >> 13));
    }
    if (exponent >= 31) {
        return static_cast<uint16_t>(sign | 0x7c00u);
    }
    uint32_t rounded = mantissa + 0x1000u;
    if ((rounded & 0x800000u) != 0) {
        rounded = 0;
        return static_cast<uint16_t>(sign | ((exponent + 1) << 10));
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) |
                                 (rounded >> 13));
}

float halfToFloat(uint16_t value)
{
    const uint32_t sign = (static_cast<uint32_t>(value) & 0x8000u) << 16;
    uint32_t exponent = (value >> 10) & 0x1fu;
    uint32_t mantissa = value & 0x3ffu;
    uint32_t bits = 0;

    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            int shift = 0;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                ++shift;
            }
            mantissa &= 0x3ffu;
            const int32_t adjusted = 127 - 15 - shift;
            bits = sign | (static_cast<uint32_t>(adjusted) << 23) |
                   (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        exponent = exponent - 15 + 127;
        bits = sign | (exponent << 23) | (mantissa << 13);
    }
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

ImageData halfPixelsToImage(int width, int height,
                            const std::vector<uint16_t>& pixels)
{
    if (width <= 0 || height <= 0 ||
        pixels.size() != static_cast<size_t>(width) * height * 4) {
        throw std::runtime_error("invalid RGBA16F image dimensions");
    }
    ImageData image;
    image.width = width;
    image.height = height;
    image.rgba.resize(pixels.size());
    for (size_t index = 0; index < pixels.size(); ++index) {
        image.rgba[index] = halfToFloat(pixels[index]);
    }
    return image;
}

ImageMetrics measureImage(const ImageData& image)
{
    ImageMetrics metrics;
    metrics.hash = fnv1a64(image.rgba.data(),
                           image.rgba.size() * sizeof(float));
    metrics.minLuminance = std::numeric_limits<float>::max();
    const size_t pixels = static_cast<size_t>(image.width) * image.height;
    double sum = 0.0;
    for (size_t pixel = 0; pixel < pixels; ++pixel) {
        const float r = image.rgba[pixel * 4 + 0];
        const float g = image.rgba[pixel * 4 + 1];
        const float b = image.rgba[pixel * 4 + 2];
        metrics.nanCount += std::isnan(r) || std::isnan(g) || std::isnan(b);
        metrics.infCount += std::isinf(r) || std::isinf(g) || std::isinf(b);
        if (std::isfinite(r) && std::isfinite(g) && std::isfinite(b)) {
            const float luminance = luminanceOf(r, g, b);
            metrics.minLuminance = std::min(metrics.minLuminance, luminance);
            metrics.maxLuminance = std::max(metrics.maxLuminance, luminance);
            sum += luminance;
        }
    }
    if (pixels == 0) {
        metrics.minLuminance = 0.0f;
    }
    metrics.meanLuminance = pixels > 0
        ? static_cast<float>(sum / static_cast<double>(pixels))
        : 0.0f;
    return metrics;
}

ProbeSample samplePixel(const ImageData& image, int x, int y)
{
    ProbeSample sample;
    sample.x = std::min(image.width - 1,
                        std::max(0, x));
    sample.y = std::min(image.height - 1,
                        std::max(0, y));
    sample.u = (static_cast<float>(sample.x) + 0.5f) /
               static_cast<float>(image.width);
    sample.v = (static_cast<float>(sample.y) + 0.5f) /
               static_cast<float>(image.height);
    const size_t offset =
        (static_cast<size_t>(sample.y) * image.width + sample.x) * 4;
    for (int channel = 0; channel < 3; ++channel) {
        sample.rgb[channel] = image.rgba[offset + channel];
    }
    sample.luminance = luminanceOf(sample.rgb[0], sample.rgb[1], sample.rgb[2]);
    return sample;
}

ProbeSample sampleProbe(const ImageData& image, float u, float v)
{
    const int x = std::min(
        image.width - 1,
        std::max(0, static_cast<int>(std::clamp(u, 0.0f, 1.0f) * image.width)));
    const int y = std::min(
        image.height - 1,
        std::max(0, static_cast<int>(std::clamp(v, 0.0f, 1.0f) * image.height)));
    return samplePixel(image, x, y);
}

BoundaryComparison compareBoundary(const ImageData& before,
                                   const ImageData& after,
                                   float probeU, float probeV)
{
    if (before.width != after.width || before.height != after.height ||
        before.rgba.size() != after.rgba.size()) {
        throw std::runtime_error("boundary images have different dimensions");
    }
    BoundaryComparison result;
    result.beforeProbe = sampleProbe(before, probeU, probeV);
    result.afterProbe = sampleProbe(after, probeU, probeV);
    for (int channel = 0; channel < 3; ++channel) {
        result.probeDeltaRgb[channel] =
            result.afterProbe.rgb[channel] - result.beforeProbe.rgb[channel];
    }
    result.probeDeltaLuminance =
        result.afterProbe.luminance - result.beforeProbe.luminance;

    const size_t pixels = static_cast<size_t>(before.width) * before.height;
    result.totalPixels = pixels;
    double sumAbs = 0.0;
    double sumSquared = 0.0;
    size_t channels = pixels * 3;
    float largestPixelDelta = -1.0f;
    int largestX = 0;
    int largestY = 0;
    for (size_t pixel = 0; pixel < pixels; ++pixel) {
        bool changed = false;
        float pixelDelta = 0.0f;
        for (int channel = 0; channel < 3; ++channel) {
            const size_t offset = pixel * 4 + channel;
            const float delta = after.rgba[offset] - before.rgba[offset];
            const float absDelta = std::abs(delta);
            sumAbs += absDelta;
            sumSquared += static_cast<double>(delta) * delta;
            result.maxAbsDifference = std::max(result.maxAbsDifference, absDelta);
            pixelDelta = std::max(pixelDelta, absDelta);
            changed = changed || absDelta > 1.0e-4f;
        }
        if (pixelDelta > largestPixelDelta) {
            largestPixelDelta = pixelDelta;
            largestX = static_cast<int>(pixel % static_cast<size_t>(before.width));
            largestY = static_cast<int>(pixel / static_cast<size_t>(before.width));
        }
        result.changedPixels += changed ? 1 : 0;
    }
    result.beforeHotspot = samplePixel(before, largestX, largestY);
    result.afterHotspot = samplePixel(after, largestX, largestY);
    for (int channel = 0; channel < 3; ++channel) {
        result.hotspotDeltaRgb[channel] =
            result.afterHotspot.rgb[channel] -
            result.beforeHotspot.rgb[channel];
    }
    result.hotspotDeltaLuminance =
        result.afterHotspot.luminance - result.beforeHotspot.luminance;
    result.meanAbsDifference = static_cast<float>(
        sumAbs / static_cast<double>(std::max<size_t>(channels, 1)));
    result.rmse = static_cast<float>(std::sqrt(
        sumSquared / static_cast<double>(std::max<size_t>(channels, 1))));
    result.psnr = safePsnr(result.rmse);
    return result;
}

void writeRawRgba16(const std::string& path,
                    const std::vector<uint16_t>& pixels)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot write " + path);
    }
    output.write(reinterpret_cast<const char*>(pixels.data()),
                 static_cast<std::streamsize>(pixels.size() * sizeof(uint16_t)));
}

void writePpm(const std::string& path, const ImageData& image)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot write " + path);
    }
    output << "P6\n" << image.width << " " << image.height << "\n255\n";
    std::vector<uint8_t> row(static_cast<size_t>(image.width) * 3);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const size_t offset =
                (static_cast<size_t>(y) * image.width + x) * 4;
            for (int channel = 0; channel < 3; ++channel) {
                float value = image.rgba[offset + channel];
                value = 1.0f - std::exp(-std::max(value, 0.0f) * 0.85f);
                value = std::pow(std::clamp(value, 0.0f, 1.0f), 1.0f / 2.2f);
                row[static_cast<size_t>(x) * 3 + channel] =
                    static_cast<uint8_t>(value * 255.0f + 0.5f);
            }
        }
        output.write(reinterpret_cast<const char*>(row.data()),
                     static_cast<std::streamsize>(row.size()));
    }
}

void writeText(const std::string& path, const std::string& text)
{
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot write " + path);
    }
    output << text;
}

CheckpointData writeCheckpoint(const std::string& path, uint64_t nextFrame,
                               const MemoryPlan& plan,
                               const FogUniforms& uniforms,
                               const std::vector<uint16_t>& halfPixels)
{
    const size_t expected = static_cast<size_t>(plan.width) * plan.height * 4;
    if (halfPixels.size() != expected) {
        throw std::runtime_error("checkpoint image size mismatch");
    }
    const CheckpointHeader header =
        makeHeader(nextFrame, plan, halfPixels.size());
    std::ostringstream memory(std::ios::binary);
    memory.write(reinterpret_cast<const char*>(&header), sizeof(header));
    memory.write(reinterpret_cast<const char*>(&uniforms), sizeof(uniforms));
    memory.write(reinterpret_cast<const char*>(halfPixels.data()),
                 static_cast<std::streamsize>(halfPixels.size() *
                                              sizeof(uint16_t)));
    const std::string payload = memory.str();
    const uint64_t hash = fnv1a64(payload.data(), payload.size());

    const std::string temporary = path + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot write " + temporary);
        }
        output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        output.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
    }
    std::filesystem::rename(temporary, path);

    CheckpointData checkpoint;
    checkpoint.path = path;
    checkpoint.fileBytes = payload.size() + sizeof(hash);
    checkpoint.hash = hash;
    checkpoint.nextFrame = nextFrame;
    checkpoint.width = plan.width;
    checkpoint.height = plan.height;
    checkpoint.tier = plan.tier;
    checkpoint.steps = plan.steps;
    checkpoint.uniforms = uniforms;
    checkpoint.halfPixels = halfPixels;
    return checkpoint;
}

CheckpointData loadCheckpoint(const std::string& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot read checkpoint " + path);
    }
    const std::streamsize fileBytes = input.tellg();
    if (fileBytes < static_cast<std::streamsize>(
            sizeof(CheckpointHeader) + sizeof(FogUniforms) + sizeof(uint64_t))) {
        throw std::runtime_error("checkpoint is too short: " + path);
    }
    input.seekg(0);
    std::vector<char> bytes(static_cast<size_t>(fileBytes));
    if (!input.read(bytes.data(), fileBytes)) {
        throw std::runtime_error("failed to read checkpoint " + path);
    }

    CheckpointHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (std::memcmp(header.magic, "FOGCP01", 7) != 0 ||
        header.version != 1 || header.headerBytes != sizeof(CheckpointHeader)) {
        throw std::runtime_error("checkpoint header is invalid");
    }
    const size_t headerAndUniforms = sizeof(CheckpointHeader) + sizeof(FogUniforms);
    if (static_cast<size_t>(fileBytes) !=
        headerAndUniforms + header.imageBytes + sizeof(uint64_t)) {
        throw std::runtime_error("checkpoint length does not match header");
    }
    const size_t expectedImageBytes =
        static_cast<size_t>(header.width) * header.height * 4 * sizeof(uint16_t);
    if (header.imageBytes != expectedImageBytes) {
        throw std::runtime_error("checkpoint image length is invalid");
    }

    uint64_t storedHash = 0;
    std::memcpy(&storedHash,
                bytes.data() + headerAndUniforms + header.imageBytes,
                sizeof(storedHash));
    const uint64_t actualHash = fnv1a64(
        bytes.data(), headerAndUniforms + header.imageBytes);
    if (storedHash != actualHash) {
        throw std::runtime_error("checkpoint hash mismatch");
    }

    CheckpointData checkpoint;
    checkpoint.path = path;
    checkpoint.fileBytes = static_cast<uint64_t>(fileBytes);
    checkpoint.hash = storedHash;
    checkpoint.nextFrame = header.nextFrame;
    checkpoint.width = static_cast<int>(header.width);
    checkpoint.height = static_cast<int>(header.height);
    checkpoint.tier = static_cast<int>(header.tier);
    checkpoint.steps = static_cast<int>(header.steps);
    std::memcpy(&checkpoint.uniforms, bytes.data() + sizeof(CheckpointHeader),
                sizeof(FogUniforms));
    checkpoint.halfPixels.resize(header.imageBytes / sizeof(uint16_t));
    std::memcpy(checkpoint.halfPixels.data(),
                bytes.data() + headerAndUniforms, header.imageBytes);
    return checkpoint;
}

std::string imageMetricsJson(const ImageMetrics& metrics)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(8)
        << "{\"hash\": \"" << std::hex << metrics.hash << std::dec << "\", "
        << "\"nan_pixels\": " << metrics.nanCount << ", "
        << "\"inf_pixels\": " << metrics.infCount << ", "
        << "\"min_luminance\": " << metrics.minLuminance << ", "
        << "\"max_luminance\": " << metrics.maxLuminance << ", "
        << "\"mean_luminance\": " << metrics.meanLuminance << "}";
    return out.str();
}

std::string probeJson(const ProbeSample& probe)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(8)
        << "{\"u\": " << probe.u << ", \"v\": " << probe.v
        << ", \"pixel\": [" << probe.x << ", " << probe.y << "], "
        << "\"rgb\": [" << probe.rgb[0] << ", " << probe.rgb[1] << ", "
        << probe.rgb[2] << "], \"luminance\": " << probe.luminance << "}";
    return out.str();
}

std::string comparisonJson(const BoundaryComparison& comparison)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(8)
        << "{\n"
        << "    \"before\": " << probeJson(comparison.beforeProbe) << ",\n"
        << "    \"after\": " << probeJson(comparison.afterProbe) << ",\n"
        << "    \"probe_delta_rgb\": [" << comparison.probeDeltaRgb[0] << ", "
        << comparison.probeDeltaRgb[1] << ", " << comparison.probeDeltaRgb[2]
        << "],\n"
        << "    \"probe_delta_luminance\": " << comparison.probeDeltaLuminance
        << ",\n"
        << "    \"hotspot_before\": " << probeJson(comparison.beforeHotspot)
        << ",\n"
        << "    \"hotspot_after\": " << probeJson(comparison.afterHotspot)
        << ",\n"
        << "    \"hotspot_delta_rgb\": [" << comparison.hotspotDeltaRgb[0]
        << ", " << comparison.hotspotDeltaRgb[1] << ", "
        << comparison.hotspotDeltaRgb[2] << "],\n"
        << "    \"hotspot_delta_luminance\": "
        << comparison.hotspotDeltaLuminance << ",\n"
        << "    \"mean_abs_difference\": " << comparison.meanAbsDifference
        << ",\n"
        << "    \"rmse\": " << comparison.rmse << ",\n"
        << "    \"psnr_db\": ";
    if (std::isfinite(comparison.psnr)) {
        out << comparison.psnr;
    } else {
        out << "\"infinity\"";
    }
    out << ",\n"
        << "    \"max_abs_difference\": " << comparison.maxAbsDifference << ",\n"
        << "    \"changed_pixels\": " << comparison.changedPixels << ",\n"
        << "    \"total_pixels\": " << comparison.totalPixels << "\n"
        << "  }";
    return out.str();
}

std::string checkpointJson(const CheckpointData& checkpoint)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(0)
        << "{\"path\": \"" << checkpoint.path << "\", "
        << "\"bytes\": " << checkpoint.fileBytes << ", "
        << "\"fnv1a64\": \"" << std::hex << checkpoint.hash << std::dec << "\", "
        << "\"next_frame\": " << checkpoint.nextFrame << ", "
        << "\"width\": " << checkpoint.width << ", "
        << "\"height\": " << checkpoint.height << ", "
        << "\"tier\": " << checkpoint.tier << ", "
        << "\"steps\": " << checkpoint.steps << "}";
    return out.str();
}

ImageData CheckpointData::toImage() const
{
    return halfPixelsToImage(width, height, halfPixels);
}
