#pragma once

#include "core/material.h"

#include <string>
#include <vector>

namespace ssor {

struct Mat4 {
    float m[16]{};
};

struct MeshVertex {
    float pos[3];
    float normal[3];
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::string name;
};

struct Camera {
    Vec3f eye;
    Vec3f target;
    Vec3f up{0.0f, 1.0f, 0.0f};
    float fovYDeg = 48.0f;
    float zNear = 0.1f;
    float zFar = 50.0f;
};

struct CameraMatrices {
    Mat4 view;
    Mat4 proj;
    Mat4 viewProj;
};

struct SceneDraw {
    std::uint32_t objectIndex = 0;
    std::uint32_t objectIdValue = 0;
    std::uint32_t repeatOf = kInvalidObject;
    std::string logicalId;
    MeshType mesh = MeshType::Cube;
    bool duplicate = false;
    Mat4 model;
    Mat4 modelView;
};

struct SceneDefinition {
    std::string name;
    std::vector<SceneDraw> draws;
};

struct SceneBuild {
    MeshData cube;
    MeshData ground;
    SceneDefinition baseline;
    SceneDefinition repeat;
    Camera camera;
    CameraMatrices cameraMatrices;
};

Mat4 mat4Identity();
Mat4 mat4Multiply(const Mat4& a, const Mat4& b);
Mat4 mat4Perspective(float fovYRad, float aspect, float zNear, float zFar);
Mat4 mat4LookAt(Vec3f eye, Vec3f target, Vec3f up);
Mat4 mat4Translate(Vec3f translation);
Mat4 mat4Scale(Vec3f scale);
Mat4 mat4RotateY(float radians);
Mat4 mat4Trs(Vec3f translation, float rotationYDeg, Vec3f scale);

CameraMatrices cameraMatrices(const Camera& camera, std::uint32_t width,
                              std::uint32_t height);

MeshData makeCubeMesh();
MeshData makeGroundMesh(float size);
SceneBuild buildScenes(const RawMaterial& material);

bool projectPoint(const Mat4& viewProj, Vec3f point, std::uint32_t width,
                  std::uint32_t height, float* px, float* py, float* viewZ);

}  // namespace ssor
