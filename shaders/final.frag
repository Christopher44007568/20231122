#version 450

layout(set = 0, binding = 0) uniform sampler2D gbuffer;
layout(set = 0, binding = 1) uniform sampler2D metrics;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 g = texture(gbuffer, vUV);
    vec4 m = texture(metrics, vUV);
    vec3 color = vec3(0.035, 0.045, 0.060);
    if (g.a > 1e-4) {
        vec3 n = normalize(g.rgb);
        float lambert = max(dot(n, normalize(vec3(0.35, 0.80, 0.45))), 0.0);
        color = vec3(0.20) + vec3(0.42) * (0.45 + 0.55 * lambert);
    }
    if (m.b > 0.5) color = mix(color, vec3(0.05, 0.75, 0.95), 0.60);
    if (m.a > 0.5) color = mix(color, vec3(1.00, 0.10, 0.06), 0.88);
    outColor = vec4(color, 1.0);
}
