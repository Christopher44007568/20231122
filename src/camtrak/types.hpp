#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace camtrak {

using Bytes = std::vector<std::uint8_t>;

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

Vec3 operator+(const Vec3& a, const Vec3& b);
Vec3 operator-(const Vec3& a, const Vec3& b);
Vec3 operator*(const Vec3& a, double s);
double dot(const Vec3& a, const Vec3& b);
double norm(const Vec3& a);

// Quaternion stored as (w, x, y, z), normalized.
struct Quat {
  double w = 1.0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

Quat normalize(const Quat& q);
Quat slerp(const Quat& a, const Quat& b, double t);
// Rotation angle in radians between two unit quaternions.
double angleBetween(const Quat& a, const Quat& b);

struct Pose {
  Vec3 position;
  Quat orientation;
};

// One captured camera sample. `t_device` is the timestamp on the capturing
// device clock (seconds); `device_id` identifies which device produced it.
struct Sample {
  double t_device = 0.0;
  std::string device_id;
  Pose pose;
};

// A render object's captured trajectory before correction.
struct Trajectory {
  std::string object_id;
  std::vector<Sample> samples;
};

enum class InterpMethod {
  Linear,   // linear position + slerp orientation
  Cubic,    // Catmull-Rom position + squad-style slerp orientation
};

struct CorrectionConfig {
  std::string object_id;
  // Missing interval on the *reference* timeline, seconds [t0, t1].
  double gap_t0 = 0.0;
  double gap_t1 = 0.0;
  // Output sample period inside the gap, seconds.
  double sample_period = 0.1;
  InterpMethod method = InterpMethod::Linear;
};

struct Metrics {
  // Computed against any retained reference samples inside the gap when the
  // run is a validation run; otherwise 0 with count == 0.
  int compared_samples = 0;
  double max_position_error = 0.0;
  double rmse_position = 0.0;
  double max_angle_error_rad = 0.0;
  double rmse_angle_rad = 0.0;
};

struct GapSegment {
  double t = 0.0;
  Pose pose;
};

struct RunResult {
  std::string run_id;
  std::string object_id;
  std::string method;
  CorrectionConfig config;
  std::vector<GapSegment> corrected;
  Metrics metrics;
};

InterpMethod parseMethod(const std::string& name);
const char* methodName(InterpMethod method);

}  // namespace camtrak
