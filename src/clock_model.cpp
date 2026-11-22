#include "clock_model.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace ph {
namespace {

constexpr double kPi = 3.14159265358979323846;

const PixelValue kColors[] = {
    {230, 60, 50, 255, 1},
    {60, 210, 110, 255, 2},
    {70, 120, 235, 255, 3},
    {235, 195, 55, 255, 4},
};

const PixelValue kBackground = {24, 28, 42, 255, 0};
const PixelValue kChecker = {32, 36, 54, 255, 0};

float wrapUnit(float value) {
    return value - std::floor(value);
}

}  // namespace

ClockStep clockStep(const ClockConfig& config, int step) {
    ClockStep result;
    result.step = step;
    result.truth_tick_us = static_cast<int64_t>(step) * config.frame_period_us;
    result.device_a_raw_us = result.truth_tick_us + config.device_a_epoch_us;

    const double scale = 1.0 + static_cast<double>(config.skew_ppm) / 1000000.0;
    const int64_t b_phase_tick = result.truth_tick_us + config.device_b_initial_phase_us;
    result.device_b_raw_us =
        config.device_b_epoch_us + static_cast<int64_t>(std::llround(b_phase_tick * scale));
    result.device_a_scene_us = result.device_a_raw_us - config.device_a_epoch_us;
    result.device_b_scene_us = result.device_b_raw_us - config.device_b_epoch_us;
    result.device_b_calibrated_scene_us =
        static_cast<int64_t>(std::llround(
            static_cast<double>(result.device_b_raw_us - config.device_b_epoch_us) / scale)) -
        config.device_b_initial_phase_us;
    result.device_b_minus_a_us = result.device_b_scene_us - result.device_a_scene_us;
    return result;
}

std::vector<Probe> makeProbes(const ClockConfig& config, int width, int height) {
    std::vector<Probe> probes;
    probes.reserve(static_cast<size_t>(config.probe_count));
    for (int i = 0; i < config.probe_count; ++i) {
        const double angle = 2.0 * kPi * static_cast<double>(i) /
                             static_cast<double>(config.probe_count);
        double u = 0.5 + static_cast<double>(config.orbit_radius) * std::cos(angle);
        double v = 0.5 + static_cast<double>(config.orbit_radius) * std::sin(angle);
        u = std::clamp(u, 0.0, 0.999999);
        v = std::clamp(v, 0.0, 0.999999);
        Probe probe;
        probe.index = i;
        std::ostringstream id;
        id << "ring_" << std::setw(3) << std::setfill('0') << i;
        probe.id = id.str();
        probe.u = u;
        probe.v = v;
        probe.x = std::min(width - 1, static_cast<int>(std::floor(u * width)));
        probe.y = std::min(height - 1, static_cast<int>(std::floor(v * height)));
        probes.push_back(std::move(probe));
    }
    return probes;
}

PixelValue evaluatePixel(const ClockConfig& config, int x, int y, int width, int height,
                         int64_t scene_tick_us) {
    const float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
    const float py = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
    const float scene_sec = static_cast<float>(scene_tick_us) / 1000000.0f;
    const float period_sec = static_cast<float>(config.object_period_us) / 1000000.0f;
    const float phase = wrapUnit(scene_sec / period_sec);

    int hit = -1;
    for (int i = 0; i < 4; ++i) {
        const float angle =
            static_cast<float>(2.0 * kPi) * (phase + static_cast<float>(i) * 0.25f);
        const float cx = 0.5f + config.orbit_radius * std::cos(angle);
        const float cy = 0.5f + config.orbit_radius * std::sin(angle);
        if (std::fabs(px - cx) <= config.object_half_extent &&
            std::fabs(py - cy) <= config.object_half_extent) {
            hit = i;
        }
    }

    if (hit >= 0) {
        PixelValue value = kColors[hit];
        value.object_id = hit + 1;
        return value;
    }

    const int checker_x = static_cast<int>(std::floor(px * 16.0f));
    const int checker_y = static_cast<int>(std::floor(py * 10.0f));
    return ((checker_x + checker_y) & 1) == 0 ? kChecker : kBackground;
}

int objectIdFromRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a != 255) {
        return -1;
    }
    for (const PixelValue& color : kColors) {
        if (color.r == r && color.g == g && color.b == b) {
            return color.object_id;
        }
    }
    if ((r == kBackground.r && g == kBackground.g && b == kBackground.b) ||
        (r == kChecker.r && g == kChecker.g && b == kChecker.b)) {
        return 0;
    }
    return -1;
}

std::string objectName(int object_id) {
    switch (object_id) {
        case 0: return "background";
        case 1: return "repeated_object_1";
        case 2: return "repeated_object_2";
        case 3: return "repeated_object_3";
        case 4: return "repeated_object_4";
        default: return "unknown";
    }
}

bool samePixel(const PixelValue& a, const PixelValue& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a &&
           a.object_id == b.object_id;
}

}  // namespace ph
