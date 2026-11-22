#version 450

layout(set = 0, binding = 0) uniform sampler2D gbuffer;
layout(std430, set = 0, binding = 1) readonly buffer BoundarySeed {
    uint seeds[];
};

layout(push_constant) uniform PC {
    vec2 texel;
    float depthThreshold;
    float normalThreshold;
    float gtDepthEps;
    float gtNormalEps;
    uint width;
    uint height;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outMetrics;
layout(location = 1) out float outBoundary;

void main() {
    vec4 center = texture(gbuffer, vUV);
    float zc = max(center.a, 1e-3);
    vec3 nc = center.rgb;
    bool cValid = dot(nc, nc) > 1e-8;
    vec2 offsets[4] = vec2[4](vec2(-1.0, 0.0), vec2(1.0, 0.0),
                              vec2(0.0, -1.0), vec2(0.0, 1.0));
    float depthDiff = 0.0;
    float normalDiff = 0.0;
    float inv[4];
    for (int i = 0; i < 4; ++i) {
        vec4 sampleValue = texture(gbuffer, vUV + offsets[i] * pc.texel);
        float zs = max(sampleValue.a, 1e-3);
        inv[i] = 1.0 / zs;
        depthDiff = max(depthDiff, abs(sampleValue.a - center.a) / zc);
        vec3 ns = sampleValue.rgb;
        bool sValid = dot(ns, ns) > 1e-8;
        if (cValid && sValid) {
            normalDiff = max(normalDiff,
                1.0 - clamp(dot(normalize(nc), normalize(ns)), -1.0, 1.0));
        }
    }
    float centerInv = 1.0 / zc;
    float depthGt = max(abs(inv[0] - 2.0 * centerInv + inv[1]),
                        abs(inv[2] - 2.0 * centerInv + inv[3]));
    bool boundary = depthGt > pc.gtDepthEps || normalDiff > pc.gtNormalEps;
    bool mask = depthDiff > pc.depthThreshold ||
                normalDiff > pc.normalThreshold;
    uint seedIndex = uint(gl_FragCoord.y) * pc.width + uint(gl_FragCoord.x);
    bool seed = seeds[seedIndex] != 0u;
    outMetrics = vec4(depthDiff, normalDiff, boundary ? 1.0 : 0.0,
                      (mask || seed) ? 1.0 : 0.0);
    outBoundary = boundary ? 1.0 : 0.0;
}
