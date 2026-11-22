#include "config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

constexpr uint64_t MiB = 1024ull * 1024ull;

std::string trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

Vec3 parseVec3(const std::string& text)
{
    std::istringstream stream(text);
    Vec3 value;
    if (!(stream >> value.x >> value.y >> value.z)) {
        throw std::runtime_error("invalid vec3 value: " + text);
    }
    return value;
}

float parseFloat(const std::string& text, const std::string& key)
{
    try {
        size_t consumed = 0;
        const float value = std::stof(text, &consumed);
        if (consumed != text.size()) {
            throw std::invalid_argument("trailing input");
        }
        return value;
    } catch (const std::exception&) {
        throw std::runtime_error("invalid float for '" + key + "': " + text);
    }
}

int parseInt(const std::string& text, const std::string& key)
{
    try {
        size_t consumed = 0;
        const int value = std::stoi(text, &consumed);
        if (consumed != text.size()) {
            throw std::invalid_argument("trailing input");
        }
        return value;
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer for '" + key + "': " + text);
    }
}

uint64_t parseUint64(const std::string& text, const std::string& key)
{
    try {
        size_t consumed = 0;
        const unsigned long long value = std::stoull(text, &consumed);
        if (consumed != text.size()) {
            throw std::invalid_argument("trailing input");
        }
        return static_cast<uint64_t>(value);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid unsigned integer for '" + key +
                                 "': " + text);
    }
}

bool parseBool(const std::string& text)
{
    return text == "1" || text == "true" || text == "yes" || text == "on";
}

uint64_t estimatePlanBytes(int width, int height, int steps)
{
    const uint64_t pixels = static_cast<uint64_t>(width) *
                            static_cast<uint64_t>(height);
    const uint64_t fogImages = pixels * 8ull * 2ull;
    const uint64_t readback = pixels * 8ull * 2ull;
    const uint64_t descriptors = 2ull * MiB;
    const uint64_t stepWorkset = static_cast<uint64_t>(steps) * 1024ull;
    return 96ull * MiB + fogImages + readback + descriptors + stepWorkset;
}

MemoryPlan makePlan(const FogConfig& config, int tier)
{
    MemoryPlan plan;
    plan.tier = tier;
    if (tier == 0) {
        plan.width = config.width;
        plan.height = config.height;
        plan.steps = config.steps;
        plan.label = "full";
    } else if (tier == 1) {
        plan.width = std::max(160, static_cast<int>(config.width * 0.75f));
        plan.height = std::max(90, static_cast<int>(config.height * 0.75f));
        plan.steps = std::max(24, static_cast<int>(config.steps * 0.75f));
        plan.label = "balanced";
    } else {
        plan.width = std::max(160, config.width / 2);
        plan.height = std::max(90, config.height / 2);
        plan.steps = std::max(16, config.steps / 2);
        plan.label = "safe";
    }
    plan.width &= ~1;
    plan.height &= ~1;
    plan.estimatedBytes = estimatePlanBytes(plan.width, plan.height, plan.steps);
    return plan;
}

float dot(const Vec3& lhs, const Vec3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3 cross(const Vec3& lhs, const Vec3& rhs)
{
    return {lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
}

Vec3 normalize(const Vec3& value)
{
    const float length = std::sqrt(std::max(dot(value, value), 1.0e-8f));
    return value * (1.0f / length);
}

} // namespace

Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 operator+(const Vec3& lhs, const Vec3& rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 operator*(const Vec3& value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

FogConfig loadFogConfig(const std::string& path)
{
    FogConfig config;
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open config: " + path);
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error(path + ":" + std::to_string(lineNumber) +
                                     ": expected key=value");
        }
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));

        if (key == "scenario") config.scenario = value;
        else if (key == "width") config.width = parseInt(value, key);
        else if (key == "height") config.height = parseInt(value, key);
        else if (key == "steps") config.steps = parseInt(value, key);
        else if (key == "max_distance") config.maxDistance = parseFloat(value, key);
        else if (key == "camera") config.camera = parseVec3(value);
        else if (key == "camera_target") config.cameraTarget = parseVec3(value);
        else if (key == "tan_half_fov") config.tanHalfFov = parseFloat(value, key);
        else if (key == "volume_min") config.volumeMin = parseVec3(value);
        else if (key == "volume_max") config.volumeMax = parseVec3(value);
        else if (key == "missing_center") config.missingCenter = parseVec3(value);
        else if (key == "missing_half") config.missingHalf = parseVec3(value);
        else if (key == "missing_enabled") config.missingEnabled = parseBool(value);
        else if (key == "missing_fallback") config.missingFallback = parseFloat(value, key);
        else if (key == "density") config.density = parseFloat(value, key);
        else if (key == "extinction") config.extinction = parseFloat(value, key);
        else if (key == "scattering") config.scattering = parseFloat(value, key);
        else if (key == "anisotropy") config.anisotropy = parseFloat(value, key);
        else if (key == "jitter") config.jitter = parseFloat(value, key);
        else if (key == "temporal_max") config.temporalMax = parseInt(value, key);
        else if (key == "warmup_frames") config.warmupFrames = parseInt(value, key);
        else if (key == "continuation_frames") {
            config.continuationFrames = parseInt(value, key);
        } else if (key == "seed") {
            config.seed = static_cast<uint32_t>(parseUint64(value, key));
        } else {
            throw std::runtime_error(path + ":" + std::to_string(lineNumber) +
                                     ": unknown key '" + key + "'");
        }
    }
    sanitizeFogConfig(config);
    return config;
}

void sanitizeFogConfig(FogConfig& config)
{
    config.width = std::clamp(config.width, 160, 1920);
    config.height = std::clamp(config.height, 90, 1080);
    config.width &= ~1;
    config.height &= ~1;
    config.steps = std::clamp(config.steps, 8, 512);
    config.maxDistance = std::clamp(config.maxDistance, 1.0f, 2000.0f);
    config.tanHalfFov = std::clamp(config.tanHalfFov, 0.05f, 2.8f);
    config.density = std::clamp(config.density, 0.0f, 256.0f);
    config.extinction = std::clamp(config.extinction, 0.0f, 64.0f);
    config.scattering = std::clamp(config.scattering, 0.0f, 16.0f);
    config.anisotropy = std::clamp(config.anisotropy, -0.95f, 0.95f);
    config.jitter = std::clamp(config.jitter, 0.0f, 1.0f);
    config.missingFallback = std::clamp(config.missingFallback, 0.0f, 1.0f);
    config.temporalMax = std::clamp(config.temporalMax, 1, 256);
    config.warmupFrames = std::clamp(config.warmupFrames, 1, 10000);
    config.continuationFrames = std::clamp(config.continuationFrames, 1, 1000);
}

FogUniforms makeFogUniforms(const FogConfig& config, uint64_t frameIndex)
{
    FogUniforms uniforms;
    const Vec3 forward = normalize(config.cameraTarget - config.camera);
    const Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    const Vec3 up = normalize(cross(right, forward));
    const float aspect = static_cast<float>(config.width) /
                         static_cast<float>(config.height);
    const Vec3 lightDirection = normalize({0.35f, 0.62f, -0.70f});

    uniforms.cameraPosSeed = {config.camera.x, config.camera.y, config.camera.z,
                              static_cast<float>(config.seed)};
    uniforms.cameraForwardTan = {forward.x, forward.y, forward.z,
                                 config.tanHalfFov};
    uniforms.cameraRightAspect = {right.x, right.y, right.z, aspect};
    uniforms.cameraUpMaxDist = {up.x, up.y, up.z, config.maxDistance};
    uniforms.volumeMinDensity = {config.volumeMin.x, config.volumeMin.y,
                                 config.volumeMin.z, config.density};
    uniforms.volumeMaxExtinction = {config.volumeMax.x, config.volumeMax.y,
                                    config.volumeMax.z, config.extinction};
    uniforms.missingCenterFallback = {config.missingCenter.x,
                                      config.missingCenter.y,
                                      config.missingCenter.z,
                                      config.missingFallback};
    uniforms.missingHalfScatter = {config.missingHalf.x, config.missingHalf.y,
                                   config.missingHalf.z, config.scattering};
    uniforms.sampling = {static_cast<float>(config.steps), config.jitter,
                         config.anisotropy,
                         static_cast<float>(config.temporalMax)};
    uniforms.frame = {static_cast<float>(frameIndex), static_cast<float>(config.width),
                      static_cast<float>(config.height),
                      config.missingEnabled ? 1.0f : 0.0f};
    uniforms.misc = {lightDirection.x, lightDirection.y, lightDirection.z, 0.0f};
    return uniforms;
}

MemoryPlan chooseMemoryPlan(const FogConfig& config, uint64_t availableBytes,
                            bool budgetWasSimulated)
{
    MemoryPlan selected = makePlan(config, 2);
    for (int tier = 0; tier <= 2; ++tier) {
        MemoryPlan candidate = makePlan(config, tier);
        if (candidate.estimatedBytes <= availableBytes) {
            selected = candidate;
            break;
        }
    }

    if (selected.tier > 0) {
        std::ostringstream reason;
        reason << (budgetWasSimulated ? "simulated" : "device")
               << " budget " << (availableBytes / MiB) << " MiB selected "
               << selected.label << " tier; full plan needs "
               << (makePlan(config, 0).estimatedBytes / MiB) << " MiB";
        selected.degradations.push_back(reason.str());
    }
    if (selected.estimatedBytes > availableBytes) {
        std::ostringstream reason;
        reason << "budget below safe minimum; clamped to safe tier (need "
               << (selected.estimatedBytes / MiB) << " MiB)";
        selected.degradations.push_back(reason.str());
    }
    return selected;
}

MemoryPlan memoryPlanForTier(const FogConfig& config, int tier)
{
    return makePlan(config, std::clamp(tier, 0, 2));
}

RunOptions parseRunOptions(int argc, char** argv)
{
    RunOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto requireValue = [&](const std::string& name) -> std::string {
            if (index + 1 >= argc) {
                throw std::runtime_error(name + " requires a value");
            }
            return argv[++index];
        };
        if (arg == "--config") {
            options.configPath = requireValue(arg);
        } else if (arg == "--evidence") {
            options.evidenceDir = requireValue(arg);
        } else if (arg == "--resume") {
            options.resumePath = requireValue(arg);
        } else if (arg == "--frames") {
            options.warmupFrames = parseInt(requireValue(arg), arg);
        } else if (arg == "--continue") {
            options.continuationFrames = parseInt(requireValue(arg), arg);
        } else if (arg == "--simulate-vram-mb") {
            options.simulatedVramMb = parseInt(requireValue(arg), arg);
        } else if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--validation") {
            options.validation = true;
        } else if (arg == "--no-present") {
            options.disablePresentation = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << usageText();
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + arg + "\n" +
                                     usageText());
        }
    }
    return options;
}

std::string usageText()
{
    return
        "fog_stability [options]\n"
        "  --config PATH             extreme fog scenario (key=value)\n"
        "  --headless                run checkpoint/resume evidence workflow\n"
        "  --resume PATH             resume from a saved checkpoint\n"
        "  --frames N                override warmup frames\n"
        "  --continue N              override continuation frames\n"
        "  --simulate-vram-mb N      force a lower memory budget\n"
        "  --evidence DIR            evidence output directory\n"
        "  --validation              enable Vulkan validation if available\n"
        "  --no-present              render without presenting (debug)\n";
}

std::string fogConfigJson(const FogConfig& config)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(6)
        << "{\n"
        << "  \"scenario\": \"" << config.scenario << "\",\n"
        << "  \"width\": " << config.width << ",\n"
        << "  \"height\": " << config.height << ",\n"
        << "  \"steps\": " << config.steps << ",\n"
        << "  \"max_distance\": " << config.maxDistance << ",\n"
        << "  \"camera\": [" << config.camera.x << ", " << config.camera.y
        << ", " << config.camera.z << "],\n"
        << "  \"volume_min\": [" << config.volumeMin.x << ", "
        << config.volumeMin.y << ", " << config.volumeMin.z << "],\n"
        << "  \"volume_max\": [" << config.volumeMax.x << ", "
        << config.volumeMax.y << ", " << config.volumeMax.z << "],\n"
        << "  \"missing_center\": [" << config.missingCenter.x << ", "
        << config.missingCenter.y << ", " << config.missingCenter.z << "],\n"
        << "  \"missing_half\": [" << config.missingHalf.x << ", "
        << config.missingHalf.y << ", " << config.missingHalf.z << "],\n"
        << "  \"density\": " << config.density << ",\n"
        << "  \"extinction\": " << config.extinction << ",\n"
        << "  \"scattering\": " << config.scattering << ",\n"
        << "  \"anisotropy\": " << config.anisotropy << ",\n"
        << "  \"jitter\": " << config.jitter << ",\n"
        << "  \"missing_enabled\": " << (config.missingEnabled ? "true" : "false") << ",\n"
        << "  \"missing_fallback\": " << config.missingFallback << ",\n"
        << "  \"temporal_max\": " << config.temporalMax << ",\n"
        << "  \"seed\": " << config.seed << ",\n"
        << "  \"warmup_frames\": " << config.warmupFrames << ",\n"
        << "  \"continuation_frames\": " << config.continuationFrames << "\n"
        << "}";
    return out.str();
}
