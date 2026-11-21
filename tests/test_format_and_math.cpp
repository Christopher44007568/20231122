// Host-side regression tests: container round trips, skeleton math and the
// typed unknown-format error path. No Vulkan device is required.

#include "../src/skeleton.hpp"
#include "../src/skinning_format.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace skr;

static int failures = 0;
#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::printf("CHECK failed: %s (%s:%d)\n", #cond, __FILE__,        \
                        __LINE__);                                            \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

static SkinnedMesh makeMesh() {
    SkinnedMesh m;
    m.objectName = "unit";
    m.bones.resize(2);
    m.bones[0].name = "root";
    m.bones[0].parent = -1;
    m.bones[0].localTransform.m[0] =
        m.bones[0].localTransform.m[5] =
        m.bones[0].localTransform.m[10] =
        m.bones[0].localTransform.m[15] = 1.0f;
    m.bones[1].name = "tip";
    m.bones[1].parent = 0;
    m.bones[1].localTransform = m.bones[0].localTransform;
    m.bones[1].localTransform.m[12] = 0.5f;
    m.bones[0].inverseBind = matInverse(m.bones[0].localTransform);
    m.bones[1].inverseBind = matInverse(
        matMultiply(m.bones[1].localTransform, m.bones[0].localTransform));
    m.poseLocal = {m.bones[0].localTransform, m.bones[1].localTransform};

    Vertex v{};
    v.position[0] = 0.25f;
    v.normal[2] = 1.0f;
    v.boneIndex[0] = 0;
    v.boneWeight[0] = 0.75f;
    v.boneIndex[1] = 1;
    v.boneWeight[1] = 0.25f;
    m.vertices.push_back(v);
    m.indices = {0, 0, 0};
    return m;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "skr_tests";
    fs::remove_all(dir);
    fs::create_directories(dir);

    SkinnedMesh original = makeMesh();
    const std::string v1Path = (dir / "a.skm").string();
    const std::string v2Path = (dir / "b.skm").string();
    writeLegacyMesh(original, v1Path);
    writeCurrentMesh(original, v2Path);

    SkinnedMesh fromV1 = loadSkinnedMesh(v1Path);
    SkinnedMesh fromV2 = loadSkinnedMesh(v2Path);
    CHECK(fromV1.revision == FormatRevision::LegacyV1);
    CHECK(fromV2.revision == FormatRevision::CurrentV2);
    CHECK(fromV1.objectName == "unit");
    CHECK(fromV1.bones.size() == 2 && fromV2.bones.size() == 2);
    CHECK(fromV1.bones[1].parent == 0);
    CHECK(fromV2.bones[0].name == "root");
    CHECK(fromV1.indices == original.indices);

    // v2 is float-exact; v1 quantizes weights to uint16.
    for (uint32_t k = 0; k < kMaxBonesPerVertex; ++k) {
        CHECK(std::fabs(fromV2.vertices[0].boneWeight[k] -
                        original.vertices[0].boneWeight[k]) < 1e-7f);
        CHECK(std::fabs(fromV1.vertices[0].boneWeight[k] -
                        original.vertices[0].boneWeight[k]) < 2e-5f);
    }

    // Skeleton math: inverse of identity is identity; skinning an identity
    // pose leaves the vertex position untouched.
    const auto skin = computeSkinningMatrices(fromV2);
    float pos[3], nrm[3];
    cpuSkinVertex(fromV2.vertices[0], skin, pos, nrm);
    CHECK(std::fabs(pos[0] - 0.25f) < 1e-5f);
    CHECK(std::fabs(pos[1]) < 1e-5f);
    CHECK(std::fabs(nrm[2] - 1.0f) < 1e-5f);

    // Unknown magic must surface as FormatError, not crash.
    const std::string badPath = (dir / "c.xyz").string();
    {
        std::ofstream out(badPath, std::ios::binary);
        const uint32_t garbage[2] = {0xDEADBEEFu, 0u};
        out.write(reinterpret_cast<const char*>(garbage), sizeof(garbage));
    }
    bool threw = false;
    try {
        (void)loadSkinnedMesh(badPath);
    } catch (const FormatError& e) {
        threw = true;
        CHECK(std::string(e.what()).find("unknown format id") !=
              std::string::npos);
    }
    CHECK(threw);

    fs::remove_all(dir);
    if (failures == 0)
        std::puts("all host-side tests passed");
    return failures == 0 ? 0 : 1;
}
