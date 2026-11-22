#include "core/scene.h"

#include <algorithm>
#include <cmath>

namespace ssor {
namespace {

constexpr float kPi = 3.14159265358979323846f;

Vec3f subtract(Vec3f a, Vec3f b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3f cross(Vec3f a, Vec3f b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

Vec3f normalize(Vec3f v) {
    const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length < 1e-8f) return {0.0f, 0.0f, 0.0f};
    return {v.x / length, v.y / length, v.z / length};
}

}  // namespace

Mat4 mat4Identity() {
    Mat4 result;
    result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0f;
    return result;
}

Mat4 mat4Multiply(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result.m[column * 4 + row] +=
                    a.m[k * 4 + row] * b.m[column * 4 + k];
            }
        }
    }
    return result;
}

Mat4 mat4Perspective(float fovYRad, float aspect, float zNear, float zFar) {
    Mat4 result;
    const float t = 1.0f / std::tan(fovYRad * 0.5f);
    result.m[0] = t / aspect;
    result.m[5] = t;
    result.m[10] = zFar / (zNear - zFar);
    result.m[11] = -1.0f;
    result.m[14] = (zFar * zNear) / (zNear - zFar);
    return result;
}

Mat4 mat4LookAt(Vec3f eye, Vec3f target, Vec3f up) {
    const Vec3f forward = normalize(subtract(target, eye));
    const Vec3f side = normalize(cross(forward, up));
    const Vec3f correctedUp = cross(side, forward);
    Mat4 result = mat4Identity();
    result.m[0] = side.x;
    result.m[4] = side.y;
    result.m[8] = side.z;
    result.m[1] = correctedUp.x;
    result.m[5] = correctedUp.y;
    result.m[9] = correctedUp.z;
    result.m[2] = -forward.x;
    result.m[6] = -forward.y;
    result.m[10] = -forward.z;
    result.m[12] = -(side.x * eye.x + side.y * eye.y + side.z * eye.z);
    result.m[13] =
        -(correctedUp.x * eye.x + correctedUp.y * eye.y + correctedUp.z * eye.z);
    result.m[14] = forward.x * eye.x + forward.y * eye.y + forward.z * eye.z;
    return result;
}

Mat4 mat4Translate(Vec3f translation) {
    Mat4 result = mat4Identity();
    result.m[12] = translation.x;
    result.m[13] = translation.y;
    result.m[14] = translation.z;
    return result;
}

Mat4 mat4Scale(Vec3f scale) {
    Mat4 result = mat4Identity();
    result.m[0] = scale.x;
    result.m[5] = scale.y;
    result.m[10] = scale.z;
    return result;
}

Mat4 mat4RotateY(float radians) {
    Mat4 result = mat4Identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result.m[0] = c;
    result.m[2] = -s;
    result.m[8] = s;
    result.m[10] = c;
    return result;
}

Mat4 mat4Trs(Vec3f translation, float rotationYDeg, Vec3f scale) {
    const float radians = rotationYDeg * kPi / 180.0f;
    return mat4Multiply(
        mat4Translate(translation),
        mat4Multiply(mat4RotateY(radians), mat4Scale(scale)));
}

CameraMatrices cameraMatrices(const Camera& camera, std::uint32_t width,
                              std::uint32_t height) {
    CameraMatrices matrices;
    matrices.view = mat4LookAt(camera.eye, camera.target, camera.up);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    matrices.proj =
        mat4Perspective(camera.fovYDeg * kPi / 180.0f, aspect, camera.zNear,
                        camera.zFar);
    matrices.viewProj = mat4Multiply(matrices.proj, matrices.view);
    return matrices;
}

MeshData makeCubeMesh() {
    MeshData mesh;
    mesh.name = "cube";
    const float p = 0.5f;
    const Vec3f faceNormals[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                  {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    const Vec3f faceU[6] = {{0, 0, -1}, {0, 0, 1}, {1, 0, 0},
                            {1, 0, 0},  {1, 0, 0}, {-1, 0, 0}};
    const Vec3f faceV[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1},
                            {0, 0, -1}, {0, 1, 0}, {0, 1, 0}};
    for (int face = 0; face < 6; ++face) {
        const Vec3f n = faceNormals[face];
        const Vec3f u = faceU[face];
        const Vec3f v = faceV[face];
        const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int j = -1; j <= 1; j += 2) {
            for (int i = -1; i <= 1; i += 2) {
                const Vec3f pos{n.x * p + u.x * p * i + v.x * p * j,
                                n.y * p + u.y * p * i + v.y * p * j,
                                n.z * p + u.z * p * i + v.z * p * j};
                mesh.vertices.push_back({{pos.x, pos.y, pos.z},
                                         {n.x, n.y, n.z}});
            }
        }
        const std::uint32_t indices[6] = {base, base + 1, base + 2,
                                          base + 2, base + 1, base + 3};
        mesh.indices.insert(mesh.indices.end(), indices, indices + 6);
    }
    return mesh;
}

MeshData makeGroundMesh(float size) {
    MeshData mesh;
    mesh.name = "ground";
    const float p = size * 0.5f;
    mesh.vertices = {{{-p, 0, -p}, {0, 1, 0}},
                     {{p, 0, -p}, {0, 1, 0}},
                     {{-p, 0, p}, {0, 1, 0}},
                     {{p, 0, p}, {0, 1, 0}}};
    mesh.indices = {0, 2, 1, 1, 2, 3};
    return mesh;
}

SceneBuild buildScenes(const RawMaterial& material) {
    SceneBuild build;
    build.cube = makeCubeMesh();
    build.ground = makeGroundMesh(10.0f);
    build.camera.eye = material.cameraEye;
    build.camera.target = material.cameraTarget;
    build.cameraMatrices =
        cameraMatrices(build.camera, material.width, material.height);

    std::vector<std::uint32_t> order(material.objects.size());
    for (std::uint32_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        if (material.objects[a].drawOrder != material.objects[b].drawOrder) {
            return material.objects[a].drawOrder < material.objects[b].drawOrder;
        }
        return a < b;
    });

    build.baseline.name = "baseline";
    build.repeat.name = "repeat";
    for (std::uint32_t index : order) {
        const ObjectDef& object = material.objects[index];
        SceneDraw draw;
        draw.objectIndex = index;
        draw.objectIdValue = index + 1;
        draw.repeatOf = object.repeatOf;
        draw.logicalId = object.id;
        draw.mesh = object.mesh;
        draw.duplicate = object.duplicate();
        draw.model = mat4Trs(object.translation, object.rotationYDeg, object.scale);
        draw.modelView = mat4Multiply(build.cameraMatrices.view, draw.model);
        build.repeat.draws.push_back(draw);
        if (!draw.duplicate) build.baseline.draws.push_back(draw);
    }
    return build;
}

bool projectPoint(const Mat4& viewProj, Vec3f point, std::uint32_t width,
                  std::uint32_t height, float* px, float* py, float* viewZ) {
    const float x = viewProj.m[0] * point.x + viewProj.m[4] * point.y +
                    viewProj.m[8] * point.z + viewProj.m[12];
    const float y = viewProj.m[1] * point.x + viewProj.m[5] * point.y +
                    viewProj.m[9] * point.z + viewProj.m[13];
    const float w = viewProj.m[3] * point.x + viewProj.m[7] * point.y +
                    viewProj.m[11] * point.z + viewProj.m[15];
    if (w <= 1e-5f) return false;
    const float ndcX = x / w;
    const float ndcY = y / w;
    *px = (ndcX * 0.5f + 0.5f) * static_cast<float>(width);
    *py = (ndcY * 0.5f + 0.5f) * static_cast<float>(height);
    if (viewZ) *viewZ = w;
    return true;
}

}  // namespace ssor
