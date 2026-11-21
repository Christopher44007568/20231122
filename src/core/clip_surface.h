#pragma once

#include "core/types.h"

#include <array>
#include <vector>

namespace clipfix {

struct ClipSettings {
    double bandMm = 0.42;
    double widthMm = 1.70;
    int curveSamples = 192;
};

struct ClipEvaluation {
    double t = 0.0;
    double lateralMm = 0.0;
    double signedOffsetMm = 0.0;
    double weight = 0.0;
    Vec3 curvePoint;
    Vec3 surfaceNormal;
};

struct ClipRunResult {
    Mesh corrected;
    RunStats stats;
    std::vector<VertexEvidence> affected;
    std::vector<VertexEvidence> keyVertices;
};

class ClipSurface {
public:
    ClipSurface(
        const std::array<Vec3, 3>& anchorPositions,
        const std::array<Vec3, 3>& anchorNormals,
        const std::array<double, 3>& offsetsMm,
        ClipSettings settings = {});

    ClipEvaluation evaluate(const Vec3& point) const;
    Vec3 pointAt(double t) const;
    Vec3 tangentAt(double t) const;
    Vec3 surfaceNormalAt(double t) const;
    const std::array<Vec3, 3>& controlPoints() const { return controlPoints_; }
    const ClipSettings& settings() const { return settings_; }

    ClipRunResult correct(const Mesh& original) const;

private:
    std::array<Vec3, 3> controlPoints_;
    ClipSettings settings_;
};

}  // namespace clipfix
