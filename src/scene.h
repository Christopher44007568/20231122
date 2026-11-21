#pragma once

#include <array>
#include <cstdint>
#include <vector>

struct Vec3 {
    float x, y, z;
};

struct Mat4 {
    float m[16]; // column-major
};

Mat4 mat4Identity();
Mat4 mat4Mul(const Mat4& a, const Mat4& b);
Mat4 mat4Perspective(float fovYRad, float aspect, float zNear, float zFar);
Mat4 mat4LookAt(Vec3 eye, Vec3 center, Vec3 up);
Mat4 mat4Translate(Vec3 t);
Mat4 mat4Scale(Vec3 s);
Mat4 mat4RotateY(float rad);
Mat4 mat4RotateX(float rad);

struct Vertex {
    float pos[3];
    float normal[3];
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

Mesh makeCube();
Mesh makePlane(float size);

struct Instance {
    const Mesh* mesh;
    Mat4 model;
};

struct Scene {
    const char* name;
    std::vector<Instance> instances;
    Vec3 eye;
    Vec3 center;
    Vec3 up;
    float fovYDeg;
};

// Built once at startup; meshes are owned by the returned struct.
struct SceneSet {
    Mesh cube;
    Mesh plane;
    std::vector<Scene> scenes;
};

SceneSet buildScenes();
