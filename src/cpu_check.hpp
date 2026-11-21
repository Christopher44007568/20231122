#pragma once

#include "skeleton.hpp"

namespace skr {

struct CpuGpuCheck {
    uint32_t verticesChecked = 0;
    double maxPositionError = 0.0;
    double maxNormalError = 0.0;
    bool consistent = false;
};

// Deterministic CPU reference for the exact skinning math the vertex shader
// performs. The tolerance covers float32 accumulation differences between the
// lavapipe/GPU vectorization and the scalar reference loop.
CpuGpuCheck verifyCpuReference(const SkinnedMesh& mesh, double tolerance = 1e-4);

} // namespace skr
