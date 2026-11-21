#include "core/clip_surface.h"

#include "core/obj_io.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace clipfix {
namespace {

double smoothStep01(double value) {
    const double x = std::clamp(value, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

Vec3 bezier(
    const std::array<Vec3, 3>& points,
    double t) {
    const double s = 1.0 - t;
    return points[0] * (s * s) + points[1] * (2.0 * s * t) + points[2] * (t * t);
}

Vec3 bezierDerivative(
    const std::array<Vec3, 3>& points,
    double t) {
    return (points[1] - points[0]) * (2.0 * (1.0 - t)) +
           (points[2] - points[1]) * (2.0 * t);
}

Vec3 chooseBinormal(const Vec3& tangent) {
    const Vec3 up{0.0, 1.0, 0.0};
    Vec3 binormal = tangent.cross(up);
    if (binormal.length() < 1.0e-8) {
        binormal = tangent.cross(Vec3{0.0, 0.0, 1.0});
    }
    return binormal.normalized(Vec3{0.0, 0.0, 1.0});
}

bool evidenceGreater(const VertexEvidence& lhs, const VertexEvidence& rhs) {
    return std::abs(lhs.deltaMm) > std::abs(rhs.deltaMm);
}

}  // namespace

ClipSurface::ClipSurface(
    const std::array<Vec3, 3>& anchorPositions,
    const std::array<Vec3, 3>& anchorNormals,
    const std::array<double, 3>& offsetsMm,
    ClipSettings settings)
    : settings_(settings) {
    settings_.curveSamples = std::max(24, settings_.curveSamples);
    settings_.bandMm = std::max(1.0e-5, settings_.bandMm);
    settings_.widthMm = std::max(1.0e-5, settings_.widthMm);
    for (std::size_t i = 0; i < 3; ++i) {
        controlPoints_[i] =
            anchorPositions[i] + anchorNormals[i].normalized() * offsetsMm[i];
    }
}

Vec3 ClipSurface::pointAt(double t) const {
    return bezier(controlPoints_, std::clamp(t, 0.0, 1.0));
}

Vec3 ClipSurface::tangentAt(double t) const {
    return bezierDerivative(controlPoints_, std::clamp(t, 0.0, 1.0))
        .normalized(Vec3{1.0, 0.0, 0.0});
}

Vec3 ClipSurface::surfaceNormalAt(double t) const {
    const Vec3 tangent = tangentAt(t);
    const Vec3 binormal = chooseBinormal(tangent);
    return binormal.cross(tangent).normalized(Vec3{0.0, 1.0, 0.0});
}

ClipEvaluation ClipSurface::evaluate(const Vec3& point) const {
    const int sampleCount = settings_.curveSamples;
    double bestT = 0.0;
    double bestDistanceSq = std::numeric_limits<double>::max();
    for (int i = 0; i <= sampleCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sampleCount);
        const Vec3 delta = point - pointAt(t);
        const double distanceSq = delta.dot(delta);
        if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            bestT = t;
        }
    }

    const double step = 1.0 / static_cast<double>(sampleCount);
    double left = std::max(0.0, bestT - step);
    double right = std::min(1.0, bestT + step);
    for (int iteration = 0; iteration < 18; ++iteration) {
        const double m1 = left + (right - left) / 3.0;
        const double m2 = right - (right - left) / 3.0;
        const Vec3 delta1 = point - pointAt(m1);
        const Vec3 delta2 = point - pointAt(m2);
        if (delta1.dot(delta1) <= delta2.dot(delta2)) {
            right = m2;
        } else {
            left = m1;
        }
    }

    ClipEvaluation result;
    result.t = std::clamp((left + right) * 0.5, 0.0, 1.0);
    const Vec3 curve = pointAt(result.t);
    const Vec3 tangent = tangentAt(result.t);
    const Vec3 binormal = chooseBinormal(tangent);
    result.surfaceNormal = binormal.cross(tangent).normalized(Vec3{0.0, 1.0, 0.0});
    result.curvePoint = curve;
    const Vec3 delta = point - curve;
    result.lateralMm = delta.dot(binormal);
    result.signedOffsetMm = delta.dot(result.surfaceNormal);

    const double bandWeight =
        smoothStep01(1.0 - std::abs(result.signedOffsetMm) / settings_.bandMm);
    const double widthWeight =
        smoothStep01(1.0 - std::abs(result.lateralMm) / settings_.widthMm);
    result.weight = bandWeight * widthWeight;
    return result;
}

ClipRunResult ClipSurface::correct(const Mesh& original) const {
    ClipRunResult result;
    result.corrected = original;
    result.stats.vertexCount = original.positions.size();

    const double count = static_cast<double>(std::max<std::size_t>(1, original.positions.size()));
    double sumBefore = 0.0;
    double sumAfter = 0.0;
    double sumDelta = 0.0;

    for (std::size_t vertex = 0; vertex < original.positions.size(); ++vertex) {
        const ClipEvaluation evaluation = evaluate(original.positions[vertex]);
        const double before = evaluation.signedOffsetMm;
        const double delta = -before * evaluation.weight;
        const double after = before + delta;

        result.corrected.positions[vertex] =
            original.positions[vertex] + evaluation.surfaceNormal * delta;

        result.stats.maxAbsBeforeMm = std::max(result.stats.maxAbsBeforeMm, std::abs(before));
        result.stats.maxAbsAfterMm = std::max(result.stats.maxAbsAfterMm, std::abs(after));
        result.stats.maxDeltaMm = std::max(result.stats.maxDeltaMm, std::abs(delta));
        sumBefore += std::abs(before);
        sumAfter += std::abs(after);
        sumDelta += std::abs(delta);

        if (std::abs(delta) > 1.0e-9) {
            ++result.stats.affectedCount;
            VertexEvidence evidence;
            evidence.vertexIndex = static_cast<std::uint32_t>(vertex);
            evidence.curveT = evaluation.t;
            evidence.lateralMm = evaluation.lateralMm;
            evidence.beforeOffsetMm = before;
            evidence.afterOffsetMm = after;
            evidence.deltaMm = delta;
            result.affected.push_back(evidence);
        }
    }

    result.stats.meanAbsBeforeMm = sumBefore / count;
    result.stats.meanAbsAfterMm = sumAfter / count;
    result.stats.meanDeltaMm = sumDelta / count;

    result.keyVertices = result.affected;
    std::sort(result.keyVertices.begin(), result.keyVertices.end(), evidenceGreater);
    if (result.keyVertices.size() > 12) {
        result.keyVertices.resize(12);
    }
    std::sort(
        result.keyVertices.begin(),
        result.keyVertices.end(),
        [](const VertexEvidence& lhs, const VertexEvidence& rhs) {
            return lhs.vertexIndex < rhs.vertexIndex;
        });

    recomputeVertexNormals(result.corrected);
    return result;
}

}  // namespace clipfix
