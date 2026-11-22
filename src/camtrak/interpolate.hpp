#pragma once

#include <vector>

#include "camtrak/types.hpp"

namespace camtrak {

// All interpolation works on the reference timeline: callers first map device
// timestamps onto the reference clock using the project clock mapping.

Vec3 lerpPosition(const Vec3& a, const Vec3& b, double u);

// Catmull-Rom position using the two neighboring samples on each side when
// available; edge samples fall back to linear behavior.
Vec3 catmullRomPosition(const Vec3& p0, const Vec3& p1, const Vec3& p2,
                        const Vec3& p3, double u);

Pose interpolateAt(const std::vector<Sample>& samples, double t,
                   InterpMethod method);

// Produces gap samples at fixed `sample_period` inside [t0, t1], including the
// exact endpoints when they are representable on the grid.
std::vector<GapSegment> fillGap(const std::vector<Sample>& samples,
                                const CorrectionConfig& config);

}  // namespace camtrak
