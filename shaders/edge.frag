#version 450
#extension GL_GOOGLE_include_directive : require
#include "edge_common.glsl"

layout(set = 0, binding = 0) uniform sampler2D gbuf;

layout(push_constant) uniform PC {
    vec2 texel;
    float depthThreshold;
    float normalThreshold;
    float gtDepthEps;
    float gtNormalEps;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outMetrics; // r=depthDiff g=normalDiff b=boundary a=finalMask

void main() {
    EdgeSample e = computeEdge(gbuf, vUV, pc.texel);
    bool boundary = e.depthGT > pc.gtDepthEps || e.normalDiff > pc.gtNormalEps;
    bool mask = e.depthDiff > pc.depthThreshold || e.normalDiff > pc.normalThreshold;
    outMetrics = vec4(e.depthDiff, e.normalDiff, boundary ? 1.0 : 0.0, mask ? 1.0 : 0.0);
}
