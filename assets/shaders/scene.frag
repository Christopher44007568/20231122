#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2DArray pageArray;

layout(push_constant) uniform PushConstants {
    int layerIndex;
    int mode;
} pc;

void main() {
    if (pc.layerIndex >= 0) {
        vec3 color = texture(pageArray, vec3(vUV, float(pc.layerIndex))).rgb;
        float scan = 0.965 + 0.035 * sin(vUV.y * 720.0);
        float edge = smoothstep(0.0, 0.025, vUV.x) *
                     smoothstep(0.0, 0.025, 1.0 - vUV.x) *
                     smoothstep(0.0, 0.035, vUV.y) *
                     smoothstep(0.0, 0.035, 1.0 - vUV.y);
        color *= scan * (0.80 + 0.20 * edge);
        outColor = vec4(color, 1.0);
        return;
    }

    vec2 cell = floor(vUV * vec2(32.0, 24.0));
    float checker = mod(cell.x + cell.y, 2.0);
    float diagonal = step(0.90, fract((vUV.x + vUV.y) * 9.0));
    vec3 base;
    vec3 stripe;
    if (pc.mode == 1) {
        base = vec3(0.060, 0.058, 0.050) + checker * 0.014;
        stripe = vec3(0.10, 0.085, 0.040);
    } else if (pc.mode == 2) {
        base = vec3(0.075, 0.047, 0.078) + checker * 0.020;
        stripe = vec3(0.20, 0.075, 0.16);
    } else {
        base = vec3(0.048, 0.052, 0.060) + checker * 0.012;
        stripe = vec3(0.18, 0.035, 0.045);
    }
    outColor = vec4(base + diagonal * stripe, 1.0);
}
