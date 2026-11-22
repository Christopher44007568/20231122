#pragma once

#include <cstdint>

#include "model.h"

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

RenderParams makeRenderParams(const Config& config,
                              const CaptureObservation& observation,
                              int totalTasks);
Vec3f shadeBokehPixel(float pixelX, float pixelY, const RenderParams& params);
Json sceneDescription(const Config& config);

