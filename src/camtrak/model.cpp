#include "camtrak/model.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace camtrak {

Vec3 operator+(const Vec3& a, const Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(const Vec3& a, const Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(const Vec3& a, double s) {
  return {a.x * s, a.y * s, a.z * s};
}

double dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

double norm(const Vec3& a) { return std::sqrt(dot(a, a)); }

Quat normalize(const Quat& q) {
  const double length =
      std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
  if (length == 0.0) return Quat{};
  return {q.w / length, q.x / length, q.y / length, q.z / length};
}

Quat slerp(const Quat& a, const Quat& b, double t) {
  Quat end = b;
  double cosine = a.w * end.w + a.x * end.x + a.y * end.y + a.z * end.z;
  if (cosine < 0.0) {
    end = {-end.w, -end.x, -end.y, -end.z};
    cosine = -cosine;
  }
  if (cosine > 0.9995) {
    return normalize(
        Quat{a.w + t * (end.w - a.w), a.x + t * (end.x - a.x),
             a.y + t * (end.y - a.y), a.z + t * (end.z - a.z)});
  }
  cosine = std::clamp(cosine, -1.0, 1.0);
  const double theta = std::acos(cosine);
  const double sin_theta = std::sin(theta);
  const double scale_a = std::sin((1.0 - t) * theta) / sin_theta;
  const double scale_b = std::sin(t * theta) / sin_theta;
  return normalize(Quat{a.w * scale_a + end.w * scale_b,
                        a.x * scale_a + end.x * scale_b,
                        a.y * scale_a + end.y * scale_b,
                        a.z * scale_a + end.z * scale_b});
}

double angleBetween(const Quat& a, const Quat& b) {
  double cosine = std::abs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z);
  cosine = std::clamp(cosine, 0.0, 1.0);
  return 2.0 * std::acos(cosine);
}

InterpMethod parseMethod(const std::string& name) {
  if (name == "linear") return InterpMethod::Linear;
  if (name == "cubic") return InterpMethod::Cubic;
  throw std::runtime_error("unknown interpolation method: " + name);
}

const char* methodName(InterpMethod method) {
  switch (method) {
    case InterpMethod::Linear:
      return "linear";
    case InterpMethod::Cubic:
      return "cubic";
  }
  return "linear";
}

void sortAndValidate(Trajectory& trajectory) {
  if (trajectory.object_id.empty()) {
    throw std::runtime_error("trajectory has empty object_id");
  }
  if (trajectory.samples.empty()) {
    throw std::runtime_error("trajectory for " + trajectory.object_id +
                             " contains no samples");
  }
  for (const Sample& sample : trajectory.samples) {
    if (sample.device_id.empty()) {
      throw std::runtime_error("sample missing device_id in object " +
                               trajectory.object_id);
    }
    const Quat& q = sample.pose.orientation;
    const double length =
        std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (!std::isfinite(length) || std::abs(length - 1.0) > 1e-6) {
      throw std::runtime_error(
          "orientation quaternion is not unit length for object " +
          trajectory.object_id);
    }
  }
  std::sort(trajectory.samples.begin(), trajectory.samples.end(),
            [](const Sample& a, const Sample& b) {
              return a.t_device < b.t_device;
            });
  for (std::size_t i = 1; i < trajectory.samples.size(); ++i) {
    if (trajectory.samples[i].t_device ==
        trajectory.samples[i - 1].t_device) {
      throw std::runtime_error(
          "duplicate device timestamp in object " + trajectory.object_id);
    }
  }
}

bool bracket(const std::vector<Sample>& samples, double t,
             const Sample*& before, const Sample*& after, double& u) {
  if (samples.size() < 2 || t < samples.front().t_device ||
      t > samples.back().t_device) {
    return false;
  }
  auto next = std::upper_bound(
      samples.begin(), samples.end(), t,
      [](double value, const Sample& sample) {
        return value < sample.t_device;
      });
  if (next == samples.end()) {
    before = &samples[samples.size() - 2];
    after = &samples.back();
  } else {
    after = &*next;
    before = &*(next - 1);
  }
  const double span = after->t_device - before->t_device;
  u = span > 0.0 ? (t - before->t_device) / span : 0.0;
  return true;
}

}  // namespace camtrak
