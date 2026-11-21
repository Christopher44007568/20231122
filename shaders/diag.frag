#version 450
#extension GL_GOOGLE_include_directive : require
#include "edge_common.glsl"

// Broken-line diagnostic: a pixel counts as "broken" when the geometric
// ground-truth boundary fires (loose epsilon) but the final outline mask
// (user thresholds) does not. Matching C++ struct: DiagRecord.
struct BrokenPixel {
    uint x;
    uint y;
    uint flags;   // bit0 depth-triggered, bit1 normal-triggered,
                  // bit2 suppressed-by-depth-threshold, bit3 suppressed-by-normal-threshold
    uint pad;
    float depthDiff;
    float normalDiff;
    float depthThreshold;
    float normalThreshold;
    vec4 dz;      // neighbourhood depth diffs  (L, R, D, U)
    vec4 dn;      // neighbourhood normal diffs (L, R, D, U)
};

layout(set = 0, binding = 0) uniform sampler2D gbuf;
layout(std430, set = 0, binding = 1) buffer Out {
    uint count;
    BrokenPixel pixels[];
} outBuf;

layout(push_constant) uniform PC {
    vec2 texel;
    float depthThreshold;
    float normalThreshold;
    float gtDepthEps;
    float gtNormalEps;
    uint maxRecords;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(0.0);
    EdgeSample e = computeEdge(gbuf, vUV, pc.texel);
    bool gtDepth = e.depthGT > pc.gtDepthEps;
    bool gtNormal = e.normalDiff > pc.gtNormalEps;
    bool boundary = gtDepth || gtNormal;
    bool depthPass = e.depthDiff > pc.depthThreshold;
    bool normalPass = e.normalDiff > pc.normalThreshold;
    bool mask = depthPass || normalPass;
    if (!boundary || mask)
        return;

    uint idx = atomicAdd(outBuf.count, 1u);
    if (idx >= pc.maxRecords)
        return;
    BrokenPixel p;
    p.x = uint(gl_FragCoord.x);
    p.y = uint(gl_FragCoord.y);
    p.flags = (gtDepth ? 1u : 0u) | (gtNormal ? 2u : 0u) |
              (!depthPass ? 4u : 0u) | (!normalPass ? 8u : 0u);
    p.pad = 0u;
    p.depthDiff = e.depthDiff;
    p.normalDiff = e.normalDiff;
    p.depthThreshold = pc.depthThreshold;
    p.normalThreshold = pc.normalThreshold;
    p.dz = e.dz;
    p.dn = e.dn;
    outBuf.pixels[idx] = p;
}
