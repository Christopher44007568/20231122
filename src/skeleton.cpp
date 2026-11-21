#include "skeleton.hpp"

#include <cmath>
#include <stdexcept>

namespace skr {

Mat4 matMultiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

Mat4 matInverse(const Mat4& m) {
    const float* o = m.m;
    Mat4 inv{};
    float* i = inv.m;
    i[0] = o[5]*o[10]*o[15] - o[5]*o[11]*o[14] - o[9]*o[6]*o[15]
         + o[9]*o[7]*o[14] + o[13]*o[6]*o[11] - o[13]*o[7]*o[10];
    i[4] = -o[4]*o[10]*o[15] + o[4]*o[11]*o[14] + o[8]*o[6]*o[15]
         - o[8]*o[7]*o[14] - o[12]*o[6]*o[11] + o[12]*o[7]*o[10];
    i[8] = o[4]*o[9]*o[15] - o[4]*o[11]*o[13] - o[8]*o[5]*o[15]
         + o[8]*o[7]*o[13] + o[12]*o[5]*o[11] - o[12]*o[7]*o[9];
    i[12] = -o[4]*o[9]*o[14] + o[4]*o[10]*o[13] + o[8]*o[5]*o[14]
          - o[8]*o[6]*o[13] - o[12]*o[5]*o[10] + o[12]*o[6]*o[9];
    i[1] = -o[1]*o[10]*o[15] + o[1]*o[11]*o[14] + o[9]*o[2]*o[15]
         - o[9]*o[3]*o[14] - o[13]*o[2]*o[11] + o[13]*o[3]*o[10];
    i[5] = o[0]*o[10]*o[15] - o[0]*o[11]*o[14] - o[8]*o[2]*o[15]
         + o[8]*o[3]*o[14] + o[12]*o[2]*o[11] - o[12]*o[3]*o[10];
    i[9] = -o[0]*o[9]*o[15] + o[0]*o[11]*o[13] + o[8]*o[1]*o[15]
         - o[8]*o[3]*o[13] - o[12]*o[1]*o[11] + o[12]*o[3]*o[9];
    i[13] = o[0]*o[9]*o[14] - o[0]*o[10]*o[13] - o[8]*o[1]*o[14]
          + o[8]*o[2]*o[13] + o[12]*o[1]*o[10] - o[12]*o[2]*o[9];
    i[2] = o[1]*o[6]*o[15] - o[1]*o[7]*o[14] - o[5]*o[2]*o[15]
         + o[5]*o[3]*o[14] + o[13]*o[2]*o[7] - o[13]*o[3]*o[6];
    i[6] = -o[0]*o[6]*o[15] + o[0]*o[7]*o[14] + o[4]*o[2]*o[15]
         - o[4]*o[3]*o[14] - o[12]*o[2]*o[7] + o[12]*o[3]*o[6];
    i[10] = o[0]*o[5]*o[15] - o[0]*o[7]*o[13] - o[4]*o[1]*o[15]
          + o[4]*o[3]*o[13] + o[12]*o[1]*o[7] - o[12]*o[3]*o[5];
    i[14] = -o[0]*o[5]*o[14] + o[0]*o[6]*o[13] + o[4]*o[1]*o[14]
          - o[4]*o[2]*o[13] - o[12]*o[1]*o[6] + o[12]*o[2]*o[5];
    i[3] = -o[1]*o[6]*o[11] + o[1]*o[7]*o[10] + o[5]*o[2]*o[11]
         - o[5]*o[3]*o[10] - o[9]*o[2]*o[7] + o[9]*o[3]*o[6];
    i[7] = o[0]*o[6]*o[11] - o[0]*o[7]*o[10] - o[4]*o[2]*o[11]
         + o[4]*o[3]*o[10] + o[8]*o[2]*o[7] - o[8]*o[3]*o[6];
    i[11] = -o[0]*o[5]*o[11] + o[0]*o[7]*o[9] + o[4]*o[1]*o[11]
          - o[4]*o[3]*o[9] - o[8]*o[1]*o[7] + o[8]*o[3]*o[5];
    i[15] = o[0]*o[5]*o[10] - o[0]*o[6]*o[9] - o[4]*o[1]*o[10]
          + o[4]*o[2]*o[9] + o[8]*o[1]*o[6] - o[8]*o[2]*o[5];
    float det = o[0]*i[0] + o[1]*i[4] + o[2]*i[8] + o[3]*i[12];
    if (std::fabs(det) < 1e-12f)
        throw std::runtime_error("singular matrix in matInverse");
    det = 1.0f / det;
    for (float& v : inv.m) v *= det;
    return inv;
}

std::vector<Mat4> computeGlobalPose(const SkinnedMesh& mesh) {
    const size_t n = mesh.bones.size();
    std::vector<Mat4> global(n);
    std::vector<int> visited(n, 0);
    for (size_t b = 0; b < n; ++b) {
        std::vector<size_t> chain;
        int32_t cur = static_cast<int32_t>(b);
        while (cur >= 0 && !visited[static_cast<size_t>(cur)]) {
            chain.push_back(static_cast<size_t>(cur));
            cur = mesh.bones[static_cast<size_t>(cur)].parent;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const size_t idx = *it;
            const int32_t parent = mesh.bones[idx].parent;
            const Mat4 local = mesh.poseLocal[idx];
            global[idx] = (parent < 0) ? local
                                      : matMultiply(local, global[static_cast<size_t>(parent)]);
            visited[idx] = 1;
        }
    }
    return global;
}

std::vector<Mat4> computeSkinningMatrices(const SkinnedMesh& mesh) {
    const std::vector<Mat4> global = computeGlobalPose(mesh);
    std::vector<Mat4> skin(mesh.bones.size());
    for (size_t b = 0; b < mesh.bones.size(); ++b)
        skin[b] = matMultiply(global[b], mesh.bones[b].inverseBind);
    return skin;
}

void cpuSkinVertex(const Vertex& v, const std::vector<Mat4>& skin,
                   float outPos[3], float outNormal[3]) {
    for (int row = 0; row < 3; ++row) {
        float p = 0.0f, nrm = 0.0f;
        for (uint32_t k = 0; k < kMaxBonesPerVertex; ++k) {
            const float w = v.boneWeight[k];
            if (w == 0.0f) continue;
            const Mat4& m = skin[v.boneIndex[k]];
            const float* pos = v.position;
            p += w * (m.m[row] * pos[0] + m.m[4 + row] * pos[1]
                    + m.m[8 + row] * pos[2] + m.m[12 + row]);
            nrm += w * (m.m[row] * v.normal[0] + m.m[4 + row] * v.normal[1]
                      + m.m[8 + row] * v.normal[2]);
        }
        outPos[row] = p;
        outNormal[row] = nrm;
    }
}

} // namespace skr
