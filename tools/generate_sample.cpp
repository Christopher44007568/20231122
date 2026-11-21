// Generates deterministic skinned-mesh fixtures in both container revisions.
// The same logical mesh is serialized as legacy v1 and current v2 so that the
// review pipeline must prove the two snapshots render identically. A third
// file with an unrecognized extension/magic exercises the error-isolation path.

#include "../src/skinning_format.hpp"
#include "../src/skeleton.hpp"

#include <cmath>
#include <fstream>
#include <iostream>

using namespace skr;

static Mat4 translation(float x, float y, float z) {
    Mat4 m{};
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
    m.m[12] = x; m.m[13] = y; m.m[14] = z;
    return m;
}

// Column-major rotation around Z: bends the ribbon in the visible XY plane.
static Mat4 rotationZ(float angle) {
    Mat4 m{};
    const float c = std::cos(angle), s = std::sin(angle);
    m.m[0] = c;  m.m[1] = s;
    m.m[4] = -s; m.m[5] = c;
    m.m[10] = m.m[15] = 1.0f;
    return m;
}

// A small ribbon mesh bent by a two-bone skeleton: root + tip.
static SkinnedMesh buildRibbon() {
    SkinnedMesh mesh;
    mesh.objectName = "ribbon";
    mesh.animationTime = 0.25f;

    mesh.bones.resize(2);
    mesh.bones[0].name = "root";
    mesh.bones[0].parent = -1;
    mesh.bones[0].localTransform = translation(-0.6f, 0.0f, 0.0f);
    mesh.bones[1].name = "tip";
    mesh.bones[1].parent = 0;
    mesh.bones[1].localTransform = translation(0.6f, 0.0f, 0.0f);

    // Inverse bind matrices: inverse of the bind-pose global transforms.
    const Mat4 rootGlobal = mesh.bones[0].localTransform;
    const Mat4 tipGlobal = matMultiply(mesh.bones[1].localTransform, rootGlobal);
    mesh.bones[0].inverseBind = matInverse(rootGlobal);
    mesh.bones[1].inverseBind = matInverse(tipGlobal);

    // Animated pose: root stays, tip rotates around Z.
    mesh.poseLocal.resize(2);
    mesh.poseLocal[0] = mesh.bones[0].localTransform;
    mesh.poseLocal[1] = matMultiply(rotationZ(0.9f),
                                    mesh.bones[1].localTransform);

    constexpr int kSegments = 8;
    for (int i = 0; i <= kSegments; ++i) {
        const float t = static_cast<float>(i) / kSegments;
        const float x = -0.6f + 1.2f * t;
        for (int row = 0; row < 2; ++row) {
            Vertex v{};
            const float y = row == 0 ? -0.35f : 0.35f;
            v.position[0] = x; v.position[1] = y; v.position[2] = 0.0f;
            v.normal[0] = 0.0f; v.normal[1] = 0.0f; v.normal[2] = 1.0f;
            v.uv[0] = t; v.uv[1] = static_cast<float>(row);
            // Smooth two-bone blend based on position along the ribbon.
            const float wTip = t;
            v.boneIndex[0] = 0; v.boneWeight[0] = 1.0f - wTip;
            v.boneIndex[1] = 1; v.boneWeight[1] = wTip;
            v.boneIndex[2] = v.boneIndex[3] = 0;
            v.boneWeight[2] = v.boneWeight[3] = 0.0f;
            mesh.vertices.push_back(v);
        }
    }
    for (int i = 0; i < kSegments; ++i) {
        const uint32_t a = static_cast<uint32_t>(i * 2);
        const uint32_t b = a + 1, c = a + 2, d = a + 3;
        mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
    }
    return mesh;
}

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : ".";
    SkinnedMesh mesh = buildRibbon();
    writeLegacyMesh(mesh, outDir + "/ribbon_legacy.skm");
    writeCurrentMesh(mesh, outDir + "/ribbon_current.skm");

    // Unknown extension with garbage magic: the driver must report a typed
    // FormatError and keep processing the remaining input objects.
    std::ofstream bad(outDir + "/broken_unknown.xyz", std::ios::binary);
    const uint32_t garbage[4] = {0xDEADBEEFu, 1, 2, 3};
    bad.write(reinterpret_cast<const char*>(garbage), sizeof(garbage));

    std::cout << "wrote ribbon_legacy.skm, ribbon_current.skm, "
                 "broken_unknown.xyz to " << outDir << '\n';
    return 0;
}
