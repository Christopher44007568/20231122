#include <cmath>
#include <stdexcept>
#include "camtrak/interpolate.hpp"
#include "camtrak/model.hpp"
#include "test_framework.hpp"

using namespace camtrak;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

Trajectory linearTrajectory() {
  Trajectory trajectory;
  trajectory.object_id = "obj";
  for (int i = 0; i <= 10; ++i) {
    Sample sample;
    sample.t_device = static_cast<double>(i);
    sample.device_id = "devA";
    sample.pose.position = Vec3{static_cast<double>(i), 0.0, 0.0};
    sample.pose.orientation = Quat{};
    trajectory.samples.push_back(sample);
  }
  return trajectory;
}

}  // namespace

TEST_CASE(interpolate_linear_midpoint) {
  Trajectory trajectory = linearTrajectory();
  Pose pose = interpolateAt(trajectory.samples, 2.5, InterpMethod::Linear);
  CHECK_NEAR(pose.position.x, 2.5);
  CHECK_NEAR(pose.position.y, 0.0);
}

TEST_CASE(interpolate_out_of_range_throws) {
  Trajectory trajectory = linearTrajectory();
  bool threw = false;
  try {
    interpolateAt(trajectory.samples, 20.0, InterpMethod::Linear);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
}

TEST_CASE(fill_gap_endpoints_and_grid) {
  Trajectory trajectory = linearTrajectory();
  CorrectionConfig config;
  config.object_id = "obj";
  config.gap_t0 = 2.0;
  config.gap_t1 = 3.0;
  config.sample_period = 0.25;
  config.method = InterpMethod::Linear;
  std::vector<GapSegment> segments = fillGap(trajectory.samples, config);
  CHECK(segments.size() == 5);
  CHECK_NEAR(segments.front().t, 2.0);
  CHECK_NEAR(segments.back().t, 3.0);
  CHECK_NEAR(segments[2].pose.position.x, 2.5);
}

TEST_CASE(slerp_matches_known_quarter_rotation) {
  const Quat a = Quat{};
  const Quat b = normalize(Quat{0.7071067811865475, 0, 0, 0.7071067811865475});
  const Quat mid = slerp(a, b, 0.5);
  // Half of a 90 degree rotation around z is 45 degrees.
  const double angle = 2.0 * std::acos(std::abs(mid.w));
  CHECK(camtrak_test::near(angle, M_PI / 4.0, 1e-9));
}

TEST_CASE(cubic_passes_through_keyframe_positions) {
  Trajectory trajectory = linearTrajectory();
  for (double t = 1.0; t <= 9.0; t += 1.0) {
    Pose pose = interpolateAt(trajectory.samples, t, InterpMethod::Cubic);
    CHECK(camtrak_test::near(pose.position.x, t, 1e-10));
  }
}

TEST_CASE(catmull_rom_midpoint_between_equal_slopes) {
  const Vec3 p0{0, 0, 0};
  const Vec3 p1{1, 0, 0};
  const Vec3 p2{2, 0, 0};
  const Vec3 p3{3, 0, 0};
  const Vec3 mid = catmullRomPosition(p0, p1, p2, p3, 0.5);
  CHECK(camtrak_test::near(mid.x, 1.5, 1e-12));
}
