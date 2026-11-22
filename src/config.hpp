#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct FogConfig {
    std::string scenario = "missing_fragment_extreme";
    int width = 640;
    int height = 360;
    int steps = 192;
    float maxDistance = 180.0f;

    Vec3 camera = {0.0f, 22.0f, 68.0f};
    Vec3 cameraTarget = {0.0f, 19.0f, 0.0f};
    float tanHalfFov = 0.62f;

    Vec3 volumeMin = {-60.0f, 0.0f, -80.0f};
    Vec3 volumeMax = {60.0f, 52.0f, 60.0f};
    Vec3 missingCenter = {8.0f, 19.0f, 6.0f};
    Vec3 missingHalf = {10.0f, 7.0f, 14.0f};
    bool missingEnabled = true;
    float missingFallback = 0.55f;

    float density = 48.0f;
    float extinction = 3.4f;
    float scattering = 1.25f;
    float anisotropy = 0.9f;
    float jitter = 0.999f;
    int temporalMax = 48;

    int warmupFrames = 24;
    int continuationFrames = 4;
    uint32_t seed = 20231122u;
};

// std140-compatible. Every member is one vec4 and the size is asserted below.
struct FogUniforms {
    alignas(16) std::array<float, 4> cameraPosSeed{};
    alignas(16) std::array<float, 4> cameraForwardTan{};
    alignas(16) std::array<float, 4> cameraRightAspect{};
    alignas(16) std::array<float, 4> cameraUpMaxDist{};
    alignas(16) std::array<float, 4> volumeMinDensity{};
    alignas(16) std::array<float, 4> volumeMaxExtinction{};
    alignas(16) std::array<float, 4> missingCenterFallback{};
    alignas(16) std::array<float, 4> missingHalfScatter{};
    alignas(16) std::array<float, 4> sampling{};
    alignas(16) std::array<float, 4> frame{};
    alignas(16) std::array<float, 4> misc{};
};

static_assert(sizeof(FogUniforms) == 176, "FogUniforms must match std140 layout");

struct MemoryPlan {
    int width = 640;
    int height = 360;
    int steps = 192;
    int tier = 0;
    uint64_t estimatedBytes = 0;
    std::string label = "full";
    std::vector<std::string> degradations;
};

struct RunOptions {
    std::string configPath;
    std::string evidenceDir = "artifacts/evidence";
    std::string resumePath;
    int warmupFrames = -1;
    int continuationFrames = -1;
    int simulatedVramMb = 0;
    bool headless = false;
    bool validation = false;
    bool disablePresentation = false;
};

Vec3 operator-(const Vec3& lhs, const Vec3& rhs);
Vec3 operator+(const Vec3& lhs, const Vec3& rhs);
Vec3 operator*(const Vec3& value, float scalar);

FogConfig loadFogConfig(const std::string& path);
void sanitizeFogConfig(FogConfig& config);
FogUniforms makeFogUniforms(const FogConfig& config, uint64_t frameIndex);

MemoryPlan chooseMemoryPlan(const FogConfig& config, uint64_t availableBytes,
                            bool budgetWasSimulated);
MemoryPlan memoryPlanForTier(const FogConfig& config, int tier);

RunOptions parseRunOptions(int argc, char** argv);
std::string usageText();
std::string fogConfigJson(const FogConfig& config);
