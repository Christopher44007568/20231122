#include "cpu_check.hpp"

#include <cmath>

namespace skr {

CpuGpuCheck verifyCpuReference(const SkinnedMesh& mesh, double tolerance) {
    CpuGpuCheck result;
    const std::vector<Mat4> skin = computeSkinningMatrices(mesh);

    for (const Vertex& v : mesh.vertices) {
        float gpuPos[3] = {0.0f, 0.0f, 0.0f};
        float gpuNormal[3] = {0.0f, 0.0f, 0.0f};

        // Mirror skinned.vert: weighted sum of per-bone transforms.
        for (uint32_t k = 0; k < kMaxBonesPerVertex; ++k) {
            const float w = v.boneWeight[k];
            if (w == 0.0f) continue;
            const Mat4& m = skin[v.boneIndex[k]];
            for (int row = 0; row < 3; ++row) {
                gpuPos[row] += w * (m.m[row] * v.position[0]
                                  + m.m[4 + row] * v.position[1]
                                  + m.m[8 + row] * v.position[2]
                                  + m.m[12 + row]);
                gpuNormal[row] += w * (m.m[row] * v.normal[0]
                                     + m.m[4 + row] * v.normal[1]
                                     + m.m[8 + row] * v.normal[2]);
            }
        }

        float refPos[3], refNormal[3];
        cpuSkinVertex(v, skin, refPos, refNormal);
        for (int row = 0; row < 3; ++row) {
            result.maxPositionError =
                std::max(result.maxPositionError,
                         static_cast<double>(std::fabs(gpuPos[row] - refPos[row])));
            result.maxNormalError =
                std::max(result.maxNormalError,
                         static_cast<double>(std::fabs(gpuNormal[row] - refNormal[row])));
        }
        ++result.verticesChecked;
    }
    result.consistent = result.maxPositionError <= tolerance &&
                        result.maxNormalError <= tolerance * 10.0;
    return result;
}

} // namespace skr
