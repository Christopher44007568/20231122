#version 450

layout(set = 0, binding = 0) uniform sampler2D metrics; // r=depthDiff g=normalDiff b=boundary a=mask
layout(set = 0, binding = 1) uniform sampler2D gbuf;    // rgb=normal a=depth

layout(push_constant) uniform PC {
    uint mode;         // 0 depth diff, 1 normal diff, 2 boundary, 3 final outline
    float depthScale;  // heatmap normalisation for depth diff
    float normalScale; // heatmap normalisation for normal diff
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 heat(float t) {
    t = clamp(t, 0.0, 1.0);
    return mix(vec3(0.02, 0.05, 0.25), vec3(1.0, 0.15, 0.05), t);
}

void main() {
    vec4 m = texture(metrics, vUV);
    if (pc.mode == 0u) {
        outColor = vec4(heat(m.r / pc.depthScale), 1.0);
    } else if (pc.mode == 1u) {
        outColor = vec4(heat(m.g / pc.normalScale), 1.0);
    } else if (pc.mode == 2u) {
        outColor = vec4(vec3(m.b), 1.0);
    } else {
        vec4 g = texture(gbuf, vUV);
        vec3 base = vec3(0.08);
        if (dot(g.rgb, g.rgb) > 1e-8) {
            float lam = 0.5 + 0.5 * clamp(dot(normalize(g.rgb), normalize(vec3(0.4, 0.7, 0.6))), 0.0, 1.0);
            base = vec3(0.25 + 0.45 * lam);
        }
        outColor = vec4(mix(base, vec3(1.0, 0.1, 0.1), m.a), 1.0);
    }
}
