#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ph {

struct ClockConfig {
    int64_t frame_period_us = 250000;
    int frame_count = 12;
    int64_t device_a_epoch_us = 900000;
    int64_t device_b_epoch_us = 4100000;
    int64_t device_b_initial_phase_us = 125000;
    int skew_ppm = 25000;
    int64_t object_period_us = 2000000;
    float object_half_extent = 0.08f;
    float orbit_radius = 0.22f;
    int probe_count = 8;
};

struct ClockStep {
    int step = 0;
    int64_t truth_tick_us = 0;
    int64_t device_a_raw_us = 0;
    int64_t device_b_raw_us = 0;
    int64_t device_a_scene_us = 0;
    int64_t device_b_scene_us = 0;
    int64_t device_b_calibrated_scene_us = 0;
    int64_t device_b_minus_a_us = 0;
};

struct Probe {
    int index = 0;
    std::string id;
    double u = 0.0;
    double v = 0.0;
    int x = 0;
    int y = 0;
};

struct PixelValue {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
    int object_id = 0;
};

ClockStep clockStep(const ClockConfig& config, int step);
std::vector<Probe> makeProbes(const ClockConfig& config, int width, int height);
PixelValue evaluatePixel(const ClockConfig& config, int x, int y, int width, int height,
                         int64_t scene_tick_us);
int objectIdFromRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
std::string objectName(int object_id);
bool samePixel(const PixelValue& a, const PixelValue& b);

}  // namespace ph
