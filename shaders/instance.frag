#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDirection = normalize(vec3(0.35, 0.75, 0.55));
    float diffuse = max(dot(normalize(inNormal), lightDirection), 0.0);
    float rim = 0.18 * (1.0 - abs(normalize(inNormal).z));
    vec3 shaded = inColor.rgb * (0.38 + 0.62 * diffuse) + vec3(rim);
    outColor = vec4(shaded, 1.0);
}
