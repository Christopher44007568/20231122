#include "scene.h"

#include <algorithm>
#include <cmath>

#include "util.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

float clampf(float value, float low, float high) {
    return std::max(low, std::min(high, value));
}

float saturate(float value) {
    return clampf(value, 0.0f, 1.0f);
}

float fractf(float value) {
    return value - std::floor(value);
}

float smoothstepf(float edge0, float edge1, float x) {
    float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float mixf(float a, float b, float t) {
    return a + (b - a) * t;
}

Vec3f mix3(const Vec3f& a, const Vec3f& b, float t) {
    return {mixf(a.x, b.x, t), mixf(a.y, b.y, t), mixf(a.z, b.z, t)};
}

Vec3f multiply(const Vec3f& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

Vec3f add(const Vec3f& a, const Vec3f& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

float hash21(float x, float y) {
    float value = std::sin(x * 127.1f + y * 311.7f) * 43758.5453123f;
    return fractf(value);
}

Vec3f hueColor(float hue) {
    Vec3f color{
        std::fabs(hue * 6.0f - 3.0f) - 1.0f,
        std::fabs(fmodf(hue * 6.0f + 2.0f, 6.0f) - 3.0f) - 1.0f,
        std::fabs(fmodf(hue * 6.0f + 4.0f, 6.0f) - 3.0f) - 1.0f,
    };
    color.x = saturate(color.x);
    color.y = saturate(color.y);
    color.z = saturate(color.z);
    return color;
}

float polygonRadius(float x, float y, float radius, float blades) {
    float angle = std::atan2(y, x);
    float sector = kTau / blades;
    float local = fmodf(angle + sector * 0.5f, sector) - sector * 0.5f;
    return radius * std::cos(kPi / blades) / std::cos(local);
}

Vec3f background(float u, float v, const RenderParams& params) {
    Vec3f low{0.006f, 0.011f, 0.027f};
    Vec3f high{0.042f, 0.024f, 0.072f};
    Vec3f color = mix3(low, high, v);
    float aspect = static_cast<float>(params.width) / static_cast<float>(params.height);
    float radial = std::sqrt((u - 0.5f) * (u - 0.5f) * aspect * aspect + (v - 0.5f) * (v - 0.5f));
    float vignette = saturate(1.0f - radial * 0.72f);
    color = multiply(color, 0.72f + 0.36f * vignette);

    float cellX = std::floor(u * 37.0f);
    float cellY = std::floor(v * 23.0f);
    if (hash21(cellX + 0.5f, cellY + 0.5f) > 0.955f) {
        float sx = (cellX + 0.5f) / 37.0f;
        float sy = (cellY + 0.5f) / 23.0f;
        float distance = std::sqrt((u - sx) * (u - sx) + (v - sy) * (v - sy));
        float sparkle = saturate(1.0f - distance * 900.0f) * 0.28f;
        color = add(color, Vec3f{sparkle * 0.72f, sparkle * 0.86f, sparkle});
    }
    return color;
}

}  // namespace

RenderParams makeRenderParams(const Config& config,
                              const CaptureObservation& observation,
                              int totalTasks) {
    RenderParams params;
    params.width = static_cast<uint32_t>(config.width);
    params.height = static_cast<uint32_t>(config.height);
    params.tileY = 0;
    params.tileHeight = static_cast<uint32_t>(config.height);
    params.frameIndex = static_cast<uint32_t>(observation.taskIndex);
    params.totalFrames = static_cast<uint32_t>(totalTasks);
    params.deviceIndex = static_cast<uint32_t>(observation.deviceIndex);
    params.seed = config.seed;
    params.timeSeconds = static_cast<float>(observation.estimatedReferenceNs / 1000000000.0);
    params.focusDepth = 0.54f;
    params.aperture = 0.115f;
    params.phase = static_cast<float>(observation.sequence) * 0.173f;
    params.clockOffsetMs = static_cast<float>(config.seed % 97U) * 0.01f;
    params.clockDriftPpm = observation.deviceIndex == 0 ? 0.0f : 1200.0f;
    params.exposure = 1.08f;
    params.padding = 0.0f;
    return params;
}

Vec3f shadeBokehPixel(float pixelX, float pixelY, const RenderParams& params) {
    float width = static_cast<float>(params.width);
    float height = static_cast<float>(params.height);
    float u = (pixelX + 0.5f) / width;
    float v = (pixelY + 0.5f) / height;
    float aspect = width / height;
    Vec3f color = background(u, v, params);
    float time = params.timeSeconds;

    for (int index = 0; index < 9; ++index) {
        float fi = static_cast<float>(index);
        float depth = index == 0 ? params.focusDepth
                                 : 0.17f + 0.79f * fractf(fi * 0.6180339f + 0.09f);
        float orbit = kTau * fractf(fi * 0.381966f + 0.13f) +
                      time * (0.055f + 0.018f * depth) * (index % 2 == 0 ? 1.0f : -1.0f);
        float drift = 0.012f * std::sin(time * 0.37f + fi * 1.7f);
        float centerX = 0.5f + std::cos(orbit) * (0.17f + 0.13f * depth) + drift;
        float centerY = 0.5f + std::sin(orbit) * (0.14f + 0.16f * depth) +
                        0.010f * std::cos(time * 0.29f + fi);
        if (index == 0) {
            centerX = 0.5f + 0.025f * std::sin(time * 0.31f);
            centerY = 0.5f + 0.018f * std::cos(time * 0.43f);
        }

        float coc = params.aperture * std::fabs(depth - params.focusDepth);
        float pulse = 0.92f + 0.08f * std::sin(time * 1.15f + fi * 0.61f);
        float radius = index == 0 ? 0.078f * pulse : (0.014f + coc * 0.56f) * pulse;
        radius *= 0.88f + 0.12f * std::sin(fi * 2.31f + time * 0.2f);
        float rotation = time * 0.09f + fi * 0.41f + static_cast<float>(params.deviceIndex) * 0.017f;
        float qx = (u - centerX) * aspect;
        float qy = v - centerY;
        float cosR = std::cos(rotation);
        float sinR = std::sin(rotation);
        float localX = qx * cosR - qy * sinR;
        float localY = qx * sinR + qy * cosR;
        float boundary = polygonRadius(localX, localY, radius, 6.0f);
        float distance = std::sqrt(localX * localX + localY * localY) - boundary;
        float aa = 1.35f / height;
        float body = 1.0f - smoothstepf(-aa, aa, distance);
        if (body <= 0.0f) continue;

        float edgeBand = radius * 0.115f;
        float edgeDistance = (distance + edgeBand * 0.82f) / edgeBand;
        float edge = std::exp(-edgeDistance * edgeDistance) * body;
        float radial = std::sqrt(localX * localX + localY * localY) /
                       std::max(boundary, 0.0001f);
        float core = std::pow(saturate(1.0f - radial), 3.0f);
        float alpha = saturate(0.115f * body + 0.50f * edge + 0.055f * core);

        float hue = 0.53f + depth * 0.31f + fi * 0.013f +
                    static_cast<float>(params.deviceIndex) * 0.018f;
        Vec3f tint = hueColor(fractf(hue));
        tint = add(multiply(tint, 0.72f), Vec3f{0.24f, 0.20f, 0.18f});
        float luminance = 0.38f + 1.05f * depth + 1.35f * edge;
        if (index == 0) luminance += 0.60f;
        tint = multiply(tint, luminance * params.exposure);
        color = mix3(color, tint, alpha);
    }

    float grain = hash21(pixelX + static_cast<float>(params.seed % 65536U),
                         pixelY + static_cast<float>(params.frameIndex * 17U));
    float dither = (grain - 0.5f) * 0.012f;
    color = add(color, Vec3f{dither, dither, dither});
    return color;
}

Json sceneDescription(const Config& config) {
    Json scene = Json::object();
    scene.set("object_uid", "obj-20231122-bokeh");
    scene.set("template", "repeated-hexagonal-aperture-disc");
    scene.set("repeated_instances", 9);
    scene.set("aperture_blades", 6);
    scene.set("focus_depth", 0.54);
    scene.set("aperture", 0.115);
    scene.set("device_observation_streams", 2);
    scene.set("frame_period_ns", static_cast<int64_t>(framePeriodNs()));
    scene.set("seed", static_cast<int64_t>(config.seed));
    scene.set("pixel_model", "analytic-polygonal-bokeh");
    return scene;
}

