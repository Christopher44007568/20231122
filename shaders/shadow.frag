#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, std140) uniform ShadowObject {
    vec4 centerRadius;
    vec4 lightStrength;
    vec4 resolutionPenumbra;
    vec4 versions;
} objectData;

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main()
{
    vec2 resolution = max(objectData.resolutionPenumbra.xy, vec2(1.0));
    vec2 p = vUv * 2.0 - 1.0;
    p.x *= resolution.x / resolution.y;

    vec2 center = objectData.centerRadius.xy;
    float radius = max(objectData.centerRadius.z, 0.001);
    float strength = clamp(objectData.lightStrength.w, 0.0, 1.0);
    float penumbra = max(objectData.resolutionPenumbra.w, 0.001);
    vec2 delta = p - center;
    float distanceToCenter = length(delta);
    float coverage = 1.0 - smoothstep(radius - penumbra, radius + penumbra,
                                      distanceToCenter);

    vec3 normal = normalize(vec3(delta, sqrt(max(radius * radius -
                                                dot(delta, delta), 0.0)) + 1e-4));
    vec3 lightDir = normalize(objectData.lightStrength.xyz);
    float lambert = clamp(dot(normal, lightDir), 0.0, 1.0);
    float shadow = mix(1.0, 0.18 + 0.82 * lambert, strength);

    float rings = 0.92 + 0.08 * sin(distanceToCenter * 46.0);
    float grain = 0.018 * hash21(gl_FragCoord.xy);
    vec3 base = vec3(0.08, 0.42, 0.78) * shadow * rings + grain;
    base += vec3(0.22, 0.06, 0.02) * smoothstep(radius, radius + penumbra * 3.0,
                                                distanceToCenter) * coverage;

    vec3 background = vec3(0.015, 0.020, 0.030);
    vec3 color = mix(background, base, coverage);
    outColor = vec4(color, 1.0);
}
