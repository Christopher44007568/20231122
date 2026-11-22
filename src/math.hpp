#pragma once

#include <cmath>
#include <cstdint>

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;
};

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct Vec4 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 0.0f;
};

struct Mat4 {
  float m[16] = {};
};

Mat4 identity();
Mat4 perspectiveVulkan(float fovYDegrees, float aspect, float nearZ, float farZ);
Vec4 transform(const Mat4& matrix, const Vec4& value);
Vec3 projectToNdc(const Mat4& viewProjection, const Vec3& world);

struct RectI {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  bool valid() const { return width > 0 && height > 0; }
  int right() const { return x + width; }
  int bottom() const { return y + height; }
};

struct ScreenPoint {
  int x = 0;
  int y = 0;

  bool inside(int width, int height) const {
    return x >= 0 && y >= 0 && x < width && y < height;
  }
};

inline float clampf(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

inline float absf(float value) { return value < 0.0f ? -value : value; }

