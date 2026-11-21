#include "core/clip_surface.h"
#include "core/obj_io.h"
#include "core/sha256.h"
#include "core/time_util.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

int main() {
    const std::string hash = clipfix::Sha256::hex("abc");
    if (hash != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
        std::fprintf(stderr, "sha256 test failed: %s\n", hash.c_str());
        return 1;
    }
    const std::array<clipfix::Vec3, 3> anchors{{
        {-1.0, 0.0, 0.0},
        {0.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
    }};
    const std::array<clipfix::Vec3, 3> normals{{
        {0.0, 1.0, 0.0},
        {0.0, 1.0, 0.0},
        {0.0, 1.0, 0.0},
    }};
    const clipfix::ClipSurface surface(anchors, normals, {0.0, 0.0, 0.0});
    const auto evaluation = surface.evaluate({0.25, 0.1, 0.0});
    if (std::abs(evaluation.signedOffsetMm - 0.1) > 1.0e-6) {
        std::fprintf(stderr, "surface distance test failed\n");
        return 1;
    }

    clipfix::Mesh mesh;
    mesh.positions = {
        {-0.2, 0.18, -0.2},
        {0.2, 0.18, -0.2},
        {0.2, 0.18, 0.2},
        {-0.2, 0.18, 0.2},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    clipfix::recomputeVertexNormals(mesh);
    const auto clipped = surface.correct(mesh);
    if (clipped.stats.affectedCount == 0 || clipped.affected.empty()) {
        std::fprintf(stderr, "clip correction did not affect vertices\n");
        return 1;
    }
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        if (std::abs(clipped.corrected.positions[i].y) >=
            std::abs(mesh.positions[i].y)) {
            std::fprintf(stderr, "clip correction did not move toward surface\n");
            return 1;
        }
    }
    for (const auto& evidence : clipped.affected) {
        if (std::abs(evidence.afterOffsetMm) >= std::abs(evidence.beforeOffsetMm)) {
            std::fprintf(stderr, "evidence offsets are inconsistent\n");
            return 1;
        }
    }

    const auto stamp = clipfix::nowTimestamp();
    if (stamp.utc.size() < 24 || stamp.utc.back() != 'Z' || stamp.epochMs <= 0) {
        std::fprintf(stderr, "timestamp formatting failed\n");
        return 1;
    }
    if (clipfix::formatFixed(-0.35, 6) != "-0.350000") {
        std::fprintf(stderr, "fixed formatting failed\n");
        return 1;
    }

    const auto temp = std::filesystem::temp_directory_path() /
        ("clipfix-unit-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".obj");
    clipfix::writeObj(temp, mesh, {"unit-test"});
    const clipfix::Mesh reloaded = clipfix::loadObj(temp);
    std::filesystem::remove(temp);
    if (reloaded.positions.size() != mesh.positions.size() ||
        reloaded.indices.size() != mesh.indices.size()) {
        std::fprintf(stderr, "OBJ round trip failed\n");
        return 1;
    }
    return 0;
}
