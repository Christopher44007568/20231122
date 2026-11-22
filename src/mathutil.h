#pragma once

#include <cmath>
#include <cstring>

// Minimal column-major mat4 / vec3 helpers (matches GLSL/SPIR-V layout).

struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
  Vec3 cross(const Vec3& o) const {
    return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
  }
  float length() const { return std::sqrt(dot(*this)); }
  Vec3 normalized() const {
    float l = length();
    return l > 1e-9f ? *this * (1.0f / l) : Vec3{0, 0, 0};
  }
};

struct Mat4 {
  float m[16]; // column-major

  static Mat4 identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
  }

  static Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
      for (int row = 0; row < 4; ++row) {
        float s = 0;
        for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
        r.m[c * 4 + row] = s;
      }
    return r;
  }

  // Transforms a point (w=1); returns xyz.
  Vec3 transformPoint(const Vec3& p) const {
    float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (std::fabs(w) > 1e-9f) {
      x /= w;
      y /= w;
      z /= w;
    }
    return {x, y, z};
  }

  static Mat4 ortho(float l, float r, float b, float t, float n, float f) {
    Mat4 o = identity();
    o.m[0] = 2.0f / (r - l);
    o.m[5] = 2.0f / (t - b);
    o.m[10] = 1.0f / (f - n); // Vulkan: z in [0,1]
    o.m[12] = -(r + l) / (r - l);
    o.m[13] = -(t + b) / (t - b);
    o.m[14] = -n / (f - n);
    return o;
  }

  static Mat4 perspective(float fovYRad, float aspect, float n, float f) {
    Mat4 p{};
    float tanHalf = std::tan(fovYRad * 0.5f);
    p.m[0] = 1.0f / (aspect * tanHalf);
    p.m[5] = 1.0f / tanHalf;
    p.m[10] = f / (f - n);
    p.m[11] = 1.0f;
    p.m[14] = -(f * n) / (f - n);
    return p;
  }

  static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 fwd = (center - eye).normalized();
    Vec3 s = fwd.cross(up).normalized();
    Vec3 u = s.cross(fwd);
    Mat4 r = identity();
    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;
    r.m[1] = u.x;
    r.m[5] = u.y;
    r.m[9] = u.z;
    r.m[2] = fwd.x;
    r.m[6] = fwd.y;
    r.m[10] = fwd.z;
    r.m[12] = -s.dot(eye);
    r.m[13] = -u.dot(eye);
    r.m[14] = -fwd.dot(eye);
    return r;
  }

  static Mat4 translate(const Vec3& v) {
    Mat4 r = identity();
    r.m[12] = v.x;
    r.m[13] = v.y;
    r.m[14] = v.z;
    return r;
  }

  static Mat4 rotateY(float rad) {
    Mat4 r = identity();
    float c = std::cos(rad), s = std::sin(rad);
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    return r;
  }

  static Mat4 scale(const Vec3& v) {
    Mat4 r = identity();
    r.m[0] = v.x;
    r.m[5] = v.y;
    r.m[10] = v.z;
    return r;
  }
};
