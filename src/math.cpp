#include "math.hpp"

#include <algorithm>

Mat4 identity() {
  Mat4 result;
  result.m[0] = 1.0f;
  result.m[5] = 1.0f;
  result.m[10] = 1.0f;
  result.m[15] = 1.0f;
  return result;
}

Mat4 perspectiveVulkan(float fovYDegrees, float aspect, float nearZ, float farZ) {
  Mat4 result;
  const float f = 1.0f / std::tan(fovYDegrees * 0.5f * 3.14159265358979323846f / 180.0f);
  result.m[0] = f / aspect;
  result.m[5] = -f;
  result.m[10] = farZ / (nearZ - farZ);
  result.m[11] = -1.0f;
  result.m[14] = (nearZ * farZ) / (nearZ - farZ);
  return result;
}

Vec4 transform(const Mat4& matrix, const Vec4& value) {
  return {
    matrix.m[0] * value.x + matrix.m[4] * value.y + matrix.m[8] * value.z + matrix.m[12] * value.w,
    matrix.m[1] * value.x + matrix.m[5] * value.y + matrix.m[9] * value.z + matrix.m[13] * value.w,
    matrix.m[2] * value.x + matrix.m[6] * value.y + matrix.m[10] * value.z + matrix.m[14] * value.w,
    matrix.m[3] * value.x + matrix.m[7] * value.y + matrix.m[11] * value.z + matrix.m[15] * value.w,
  };
}

Vec3 projectToNdc(const Mat4& viewProjection, const Vec3& world) {
  const Vec4 clip = transform(viewProjection, {world.x, world.y, world.z, 1.0f});
  if (std::abs(clip.w) < 1e-6f) {
    return {};
  }
  return {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}

