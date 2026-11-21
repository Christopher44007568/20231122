#include "scene.h"

#include <cmath>

Mat4 mat4Identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 mat4Mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                r.m[c * 4 + row] += a.m[k * 4 + row] * b.m[c * 4 + k];
    return r;
}

Mat4 mat4Perspective(float fovYRad, float aspect, float zNear, float zFar) {
    Mat4 r{};
    float t = 1.0f / std::tan(fovYRad * 0.5f);
    r.m[0] = t / aspect;
    r.m[5] = t;
    r.m[10] = zFar / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (zFar * zNear) / (zNear - zFar);
    return r;
}

static Vec3 vsub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 vcross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static Vec3 vnorm(Vec3 a) {
    float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return {a.x / l, a.y / l, a.z / l};
}

Mat4 mat4LookAt(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = vnorm(vsub(center, eye));
    Vec3 s = vnorm(vcross(f, up));
    Vec3 u = vcross(s, f);
    Mat4 r = mat4Identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
    r.m[13] = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
    r.m[14] = (f.x * eye.x + f.y * eye.y + f.z * eye.z);
    return r;
}

Mat4 mat4Translate(Vec3 t) {
    Mat4 r = mat4Identity();
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}

Mat4 mat4Scale(Vec3 s) {
    Mat4 r = mat4Identity();
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}

Mat4 mat4RotateY(float rad) {
    Mat4 r = mat4Identity();
    float c = std::cos(rad), s = std::sin(rad);
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}

Mat4 mat4RotateX(float rad) {
    Mat4 r = mat4Identity();
    float c = std::cos(rad), s = std::sin(rad);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}

Mesh makeCube() {
    Mesh m;
    const float p = 0.5f;
    const Vec3 faceN[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const Vec3 faceU[6] = {{0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}};
    const Vec3 faceV[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, 1, 0}};
    for (int f = 0; f < 6; ++f) {
        Vec3 n = faceN[f], u = faceU[f], v = faceV[f];
        uint32_t base = (uint32_t)m.vertices.size();
        for (int j = -1; j <= 1; j += 2)
            for (int i = -1; i <= 1; i += 2) {
                Vec3 pos{n.x * p + u.x * p * i + v.x * p * j,
                         n.y * p + u.y * p * i + v.y * p * j,
                         n.z * p + u.z * p * i + v.z * p * j};
                m.vertices.push_back({{pos.x, pos.y, pos.z}, {n.x, n.y, n.z}});
            }
        uint32_t idx[6] = {base, base + 1, base + 2, base + 2, base + 1, base + 3};
        for (uint32_t k : idx) m.indices.push_back(k);
    }
    return m;
}

Mesh makePlane(float size) {
    Mesh m;
    float p = size * 0.5f;
    m.vertices = {{{-p, 0, -p}, {0, 1, 0}}, {{p, 0, -p}, {0, 1, 0}},
                  {{-p, 0, p}, {0, 1, 0}},  {{p, 0, p}, {0, 1, 0}}};
    m.indices = {0, 2, 1, 1, 2, 3};
    return m;
}

static Mat4 trs(Vec3 t, float rotYDeg, Vec3 s) {
    const float kDeg = 3.14159265f / 180.0f;
    return mat4Mul(mat4Translate(t), mat4Mul(mat4RotateY(rotYDeg * kDeg), mat4Scale(s)));
}

SceneSet buildScenes() {
    SceneSet ss;
    ss.cube = makeCube();
    ss.plane = makePlane(10.0f);

    // 1) Plane: ground plane with a cube resting on it.
    {
        Scene s;
        s.name = "plane";
        s.instances.push_back({&ss.plane, mat4Identity()});
        s.instances.push_back({&ss.cube, trs({0.0f, 0.5f, 0.0f}, 25.0f, {1, 1, 1})});
        s.eye = {3.2f, 2.4f, 4.0f};
        s.center = {0.0f, 0.4f, 0.0f};
        s.up = {0, 1, 0};
        s.fovYDeg = 50.0f;
        ss.scenes.push_back(s);
    }
    // 2) Thin sheet: a 0.04-thick slab standing on the ground plane.
    {
        Scene s;
        s.name = "thin_sheet";
        s.instances.push_back({&ss.plane, mat4Identity()});
        s.instances.push_back({&ss.cube, trs({0.0f, 1.0f, 0.0f}, 65.0f, {2.4f, 2.0f, 0.04f})});
        s.eye = {1.6f, 1.6f, 3.8f};
        s.center = {0.0f, 0.9f, 0.0f};
        s.up = {0, 1, 0};
        s.fovYDeg = 50.0f;
        ss.scenes.push_back(s);
    }
    // 3) Intersecting models: two cubes driven through each other.
    {
        Scene s;
        s.name = "intersecting";
        s.instances.push_back({&ss.cube, trs({-0.35f, 0.8f, 0.0f}, 20.0f, {1.6f, 1.6f, 1.6f})});
        s.instances.push_back({&ss.cube, trs({0.45f, 0.9f, 0.1f}, -35.0f, {1.6f, 1.6f, 1.6f})});
        s.eye = {2.8f, 2.2f, 3.6f};
        s.center = {0.0f, 0.8f, 0.0f};
        s.up = {0, 1, 0};
        s.fovYDeg = 50.0f;
        ss.scenes.push_back(s);
    }
    return ss;
}
