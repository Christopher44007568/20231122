#include "camtrak/interpolate.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "camtrak/model.hpp"

namespace camtrak {

Vec3 lerpPosition(const Vec3& a, const Vec3& b, double u) {
  return a + (b - a) * u;
}

Vec3 catmullRomPosition(const Vec3& p0, const Vec3& p1, const Vec3& p2,
                        const Vec3& p3, double u) {
  const double u2 = u * u;
  const double u3 = u2 * u;
  auto scale = [](const Vec3& v, double s) { return v * s; };
  const Vec3 term0 = scale(p0, -0.5 * u3 + u2 - 0.5 * u);
  const Vec3 term1 = scale(p1, 1.5 * u3 - 2.5 * u2 + 1.0);
  const Vec3 term2 = scale(p2, -1.5 * u3 + 2.0 * u2 + 0.5 * u);
  const Vec3 term3 = scale(p3, 0.5 * u3 - 0.5 * u2);
  return term0 + term1 + term2 + term3;
}

Pose interpolateAt(const std::vector<Sample>& samples, double t,
                   InterpMethod method) {
  const Sample *before = nullptr;
  const Sample *after = nullptr;
  double u = 0.0;
  if (!bracket(samples, t, before, after, u)) {
    throw std::runtime_error(
        "interpolation target lies outside the sampled trajectory range");
  }

  Vec3 position;
  if (method == InterpMethod::Cubic) {
    const auto index_of = [&](const Sample* pointer) {
      return static_cast<std::size_t>(pointer - samples.data());
    };
    const std::size_t i1 = index_of(before);
    const std::size_t i2 = i1 + 1;
    const Vec3& p0 = samples[i1 == 0 ? 0 : i1 - 1].pose.position;
    const Vec3& p1 = before->pose.position;
    const Vec3& p2 = after->pose.position;
    const Vec3& p3 =
        samples[i2 + 1 >= samples.size() ? samples.size() - 1 : i2 + 1]
            .pose.position;
    position = catmullRomPosition(p0, p1, p2, p3, u);
  } else {
    position = lerpPosition(before->pose.position, after->pose.position, u);
  }
  return Pose{position, slerp(before->pose.orientation,
                              after->pose.orientation, u)};
}

std::vector<GapSegment> fillGap(const std::vector<Sample>& samples,
                                const CorrectionConfig& config) {
  if (!(config.gap_t1 > config.gap_t0)) {
    throw std::runtime_error(
        "gap interval must have a positive, ordered extent");
  }
  if (!(config.sample_period > 0.0)) {
    throw std::runtime_error("sample period must be positive");
  }

  std::vector<GapSegment> result;
  const std::size_t steps = static_cast<std::size_t>(
      std::llround((config.gap_t1 - config.gap_t0) / config.sample_period));
  for (std::size_t i = 0; i <= steps; ++i) {
    const double t = config.gap_t0 +
                     static_cast<double>(i) * config.sample_period;
    const double clamped = std::min(t, config.gap_t1);
    result.push_back(
        GapSegment{clamped, interpolateAt(samples, clamped, config.method)});
  }
  return result;
}

}  // namespace camtrak
