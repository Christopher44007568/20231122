#pragma once

#include "skinning_format.hpp"

namespace skr {

// Column-major matrix helpers shared by CPU cross-checks and pose preparation.
Mat4 matMultiply(const Mat4& a, const Mat4& b);
Mat4 matInverse(const Mat4& m);

// Walks the parent chain and produces the object-space pose matrices.
// Global[b] = Local[b] * Global[parent(b)]  (column-major convention).
std::vector<Mat4> computeGlobalPose(const SkinnedMesh& mesh);

// Final skinning matrices uploaded to the GPU:
//   skin[b] = globalPose[b] * inverseBind[b]
std::vector<Mat4> computeSkinningMatrices(const SkinnedMesh& mesh);

// CPU reference skinning of a single vertex, used to validate the GPU path.
void cpuSkinVertex(const Vertex& v, const std::vector<Mat4>& skin,
                   float outPos[3], float outNormal[3]);

} // namespace skr
