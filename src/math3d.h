#pragma once
#include <cmath>
#include <cstddef>

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
    return l > 1e-8f ? (*this) * (1.0f / l) : Vec3{0, 0, 0};
  }
};

struct Vec4 {
  float x = 0, y = 0, z = 0, w = 0;
};

// Column-major 4x4, matches GLSL std140 mat4 layout.
struct Mat4 {
  float m[16];

  static Mat4 identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
  }
  static Mat4 zero() {
    Mat4 r{};
    for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
    return r;
  }
  float& at(int col, int row) { return m[col * 4 + row]; }
  float at(int col, int row) const { return m[col * 4 + row]; }

  Mat4 operator*(const Mat4& o) const {
    Mat4 r = Mat4::zero();
    for (int c = 0; c < 4; ++c)
      for (int rr = 0; rr < 4; ++rr)
        for (int k = 0; k < 4; ++k) r.at(c, rr) += at(k, rr) * o.at(c, k);
    return r;
  }
  Vec4 operator*(const Vec4& v) const {
    Vec4 r;
    r.x = at(0, 0) * v.x + at(1, 0) * v.y + at(2, 0) * v.z + at(3, 0) * v.w;
    r.y = at(0, 1) * v.x + at(1, 1) * v.y + at(2, 1) * v.z + at(3, 1) * v.w;
    r.z = at(0, 2) * v.x + at(1, 2) * v.y + at(2, 2) * v.z + at(3, 2) * v.w;
    r.w = at(0, 3) * v.x + at(1, 3) * v.y + at(2, 3) * v.z + at(3, 3) * v.w;
    return r;
  }

  static Mat4 translation(const Vec3& t) {
    Mat4 r = identity();
    r.at(3, 0) = t.x;
    r.at(3, 1) = t.y;
    r.at(3, 2) = t.z;
    return r;
  }
  static Mat4 scaling(const Vec3& s) {
    Mat4 r = identity();
    r.at(0, 0) = s.x;
    r.at(1, 1) = s.y;
    r.at(2, 2) = s.z;
    return r;
  }
  static Mat4 rotationY(float rad) {
    Mat4 r = identity();
    float c = std::cos(rad), s = std::sin(rad);
    r.at(0, 0) = c;  r.at(0, 2) = -s;
    r.at(2, 0) = s;  r.at(2, 2) = c;
    return r;
  }

  // Right-handed lookAt, camera looks down -Z.
  static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = (center - eye).normalized();
    Vec3 s = f.cross(up).normalized();
    Vec3 u = s.cross(f);
    Mat4 r = identity();
    r.at(0, 0) = s.x;  r.at(1, 0) = s.y;  r.at(2, 0) = s.z;
    r.at(0, 1) = u.x;  r.at(1, 1) = u.y;  r.at(2, 1) = u.z;
    r.at(0, 2) = -f.x; r.at(1, 2) = -f.y; r.at(2, 2) = -f.z;
    r.at(3, 0) = -s.dot(eye);
    r.at(3, 1) = -u.dot(eye);
    r.at(3, 2) = f.dot(eye);
    return r;
  }

  // Vulkan perspective: z in [0,1], Y flipped for viewport handled by caller.
  static Mat4 perspective(float fovYRad, float aspect, float zNear, float zFar) {
    Mat4 r = Mat4::zero();
    float t = 1.0f / std::tan(fovYRad * 0.5f);
    r.at(0, 0) = t / aspect;
    r.at(1, 1) = t;
    r.at(2, 2) = zFar / (zNear - zFar);
    r.at(2, 3) = -1.0f;
    r.at(3, 2) = (zFar * zNear) / (zNear - zFar);
    return r;
  }

  // Vulkan ortho: z in [0,1].
  static Mat4 ortho(float left, float right, float bottom, float top,
                    float zNear, float zFar) {
    Mat4 r = identity();
    r.at(0, 0) = 2.0f / (right - left);
    r.at(1, 1) = 2.0f / (top - bottom);
    r.at(2, 2) = -1.0f / (zFar - zNear);
    r.at(3, 0) = -(right + left) / (right - left);
    r.at(3, 1) = -(top + bottom) / (top - bottom);
    r.at(3, 2) = -zNear / (zFar - zNear);
    return r;
  }

  // Full 4x4 inverse (adjugate method).
  Mat4 inverted() const {
    Mat4 inv{};
    const float* a = m;
    inv.m[0] = a[5]*a[10]*a[15]-a[5]*a[11]*a[14]-a[9]*a[6]*a[15]+a[9]*a[7]*a[14]+a[13]*a[6]*a[11]-a[13]*a[7]*a[10];
    inv.m[4] = -a[4]*a[10]*a[15]+a[4]*a[11]*a[14]+a[8]*a[6]*a[15]-a[8]*a[7]*a[14]-a[12]*a[6]*a[11]+a[12]*a[7]*a[10];
    inv.m[8] = a[4]*a[9]*a[15]-a[4]*a[11]*a[13]-a[8]*a[5]*a[15]+a[8]*a[7]*a[13]+a[12]*a[5]*a[11]-a[12]*a[7]*a[9];
    inv.m[12] = -a[4]*a[9]*a[14]+a[4]*a[10]*a[13]+a[8]*a[5]*a[14]-a[8]*a[6]*a[13]-a[12]*a[5]*a[10]+a[12]*a[6]*a[9];
    inv.m[1] = -a[1]*a[10]*a[15]+a[1]*a[11]*a[14]+a[9]*a[2]*a[15]-a[9]*a[3]*a[14]-a[13]*a[2]*a[11]+a[13]*a[3]*a[10];
    inv.m[5] = a[0]*a[10]*a[15]-a[0]*a[11]*a[14]-a[8]*a[2]*a[15]+a[8]*a[3]*a[14]+a[12]*a[2]*a[11]-a[12]*a[3]*a[10];
    inv.m[9] = -a[0]*a[9]*a[15]+a[0]*a[11]*a[13]+a[8]*a[1]*a[15]-a[8]*a[3]*a[13]-a[12]*a[1]*a[11]+a[12]*a[3]*a[9];
    inv.m[13] = a[0]*a[9]*a[14]-a[0]*a[10]*a[13]-a[8]*a[1]*a[14]+a[8]*a[2]*a[13]+a[12]*a[1]*a[10]-a[12]*a[2]*a[9];
    inv.m[2] = a[1]*a[6]*a[15]-a[1]*a[7]*a[14]-a[5]*a[2]*a[15]+a[5]*a[3]*a[14]+a[13]*a[2]*a[7]-a[13]*a[3]*a[6];
    inv.m[6] = -a[0]*a[6]*a[15]+a[0]*a[7]*a[14]+a[4]*a[2]*a[15]-a[4]*a[3]*a[14]-a[12]*a[2]*a[7]+a[12]*a[3]*a[6];
    inv.m[10] = a[0]*a[5]*a[15]-a[0]*a[7]*a[13]-a[4]*a[1]*a[15]+a[4]*a[3]*a[13]+a[12]*a[1]*a[7]-a[12]*a[3]*a[5];
    inv.m[14] = -a[0]*a[5]*a[14]+a[0]*a[6]*a[13]+a[4]*a[1]*a[14]-a[4]*a[2]*a[13]-a[12]*a[1]*a[6]+a[12]*a[2]*a[5];
    inv.m[3] = -a[1]*a[6]*a[11]+a[1]*a[7]*a[10]+a[5]*a[2]*a[11]-a[5]*a[3]*a[10]-a[9]*a[2]*a[7]+a[9]*a[3]*a[6];
    inv.m[7] = a[0]*a[6]*a[11]-a[0]*a[7]*a[10]-a[4]*a[2]*a[11]+a[4]*a[3]*a[10]+a[8]*a[2]*a[7]-a[8]*a[3]*a[6];
    inv.m[11] = -a[0]*a[5]*a[11]+a[0]*a[7]*a[9]+a[4]*a[1]*a[11]-a[4]*a[3]*a[9]-a[8]*a[1]*a[7]+a[8]*a[3]*a[5];
    inv.m[15] = a[0]*a[5]*a[10]-a[0]*a[6]*a[9]-a[4]*a[1]*a[10]+a[4]*a[2]*a[9]+a[8]*a[1]*a[6]-a[8]*a[2]*a[5];
    float det = a[0]*inv.m[0] + a[1]*inv.m[4] + a[2]*inv.m[8] + a[3]*inv.m[12];
    Mat4 r{};
    float invDet = det != 0.0f ? 1.0f / det : 0.0f;
    for (int i = 0; i < 16; ++i) r.m[i] = inv.m[i] * invDet;
    return r;
  }
};
