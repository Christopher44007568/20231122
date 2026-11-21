#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUv;
layout(location = 2) in float inWeightCoverage;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

void main() {
    vec3 lightDir = normalize(vec3(0.4, 0.8, 0.6));
    float diffuse = max(dot(normalize(inNormal), lightDir), 0.0);

    // UV-tinted base so both geometry motion and attribute wiring are visible.
    vec3 base = vec3(0.15 + 0.55 * inUv.x, 0.25, 0.45 + 0.35 * inUv.y);
    vec3 color = base * (0.35 + 0.65 * diffuse);

    // Red channel diagnostic: vertices whose weights do not sum to ~1.0.
    float coverageError = abs(inWeightCoverage - 1.0);
    color = mix(color, vec3(1.0, 0.0, 0.0), clamp(coverageError * 10.0, 0.0, 1.0));

    outColor = vec4(color, 1.0);
}
