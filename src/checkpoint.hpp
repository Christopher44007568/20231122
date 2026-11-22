#pragma once

#include "config.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<float> rgba;
};

struct ImageMetrics {
    uint64_t hash = 0;
    size_t nanCount = 0;
    size_t infCount = 0;
    float minLuminance = 0.0f;
    float maxLuminance = 0.0f;
    float meanLuminance = 0.0f;
};

struct ProbeSample {
    float u = 0.0f;
    float v = 0.0f;
    int x = 0;
    int y = 0;
    float rgb[3] = {0.0f, 0.0f, 0.0f};
    float luminance = 0.0f;
};

struct BoundaryComparison {
    ProbeSample beforeProbe;
    ProbeSample afterProbe;
    ProbeSample beforeHotspot;
    ProbeSample afterHotspot;
    float probeDeltaRgb[3] = {0.0f, 0.0f, 0.0f};
    float probeDeltaLuminance = 0.0f;
    float hotspotDeltaRgb[3] = {0.0f, 0.0f, 0.0f};
    float hotspotDeltaLuminance = 0.0f;
    float meanAbsDifference = 0.0f;
    float rmse = 0.0f;
    float maxAbsDifference = 0.0f;
    size_t changedPixels = 0;
    size_t totalPixels = 0;
    double psnr = 0.0;
};

struct CheckpointHeader {
    char magic[8] = {'F', 'O', 'G', 'C', 'P', '0', '1', '\0'};
    uint32_t version = 1;
    uint32_t headerBytes = 0;
    uint64_t nextFrame = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t tier = 0;
    uint32_t steps = 0;
    uint64_t imageBytes = 0;
};

static_assert(sizeof(CheckpointHeader) == 48,
              "checkpoint header must remain a fixed-size format");

struct CheckpointData {
    std::string path;
    uint64_t fileBytes = 0;
    uint64_t hash = 0;
    uint64_t nextFrame = 0;
    int width = 0;
    int height = 0;
    int tier = 0;
    int steps = 0;
    FogUniforms uniforms{};
    std::vector<uint16_t> halfPixels;

    ImageData toImage() const;
};

uint64_t fnv1a64(const void* data, size_t bytes, uint64_t seed = 1469598103934665603ull);
uint64_t fnv1a64(const std::vector<uint16_t>& values);
uint16_t floatToHalf(float value);
float halfToFloat(uint16_t value);
ImageData halfPixelsToImage(int width, int height,
                            const std::vector<uint16_t>& pixels);

ImageMetrics measureImage(const ImageData& image);
ProbeSample samplePixel(const ImageData& image, int x, int y);
ProbeSample sampleProbe(const ImageData& image, float u, float v);
BoundaryComparison compareBoundary(const ImageData& before,
                                   const ImageData& after,
                                   float probeU, float probeV);

void writeRawRgba16(const std::string& path, const std::vector<uint16_t>& pixels);
void writePpm(const std::string& path, const ImageData& image);
void writeText(const std::string& path, const std::string& text);

CheckpointData writeCheckpoint(const std::string& path, uint64_t nextFrame,
                               const MemoryPlan& plan,
                               const FogUniforms& uniforms,
                               const std::vector<uint16_t>& halfPixels);
CheckpointData loadCheckpoint(const std::string& path);

std::string imageMetricsJson(const ImageMetrics& metrics);
std::string probeJson(const ProbeSample& probe);
std::string comparisonJson(const BoundaryComparison& comparison);
std::string checkpointJson(const CheckpointData& checkpoint);
