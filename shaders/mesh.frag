#version 450

layout(location = 0) in vec3 fragmentColor;
layout(location = 1) in vec3 fragmentNormal;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDirection = normalize(vec3(0.45, 0.85, 0.30));
    float normalLength = length(fragmentNormal);
    float lighting = 1.0;
    if (normalLength > 0.25) {
        lighting = 0.24 + 0.76 * max(dot(normalize(fragmentNormal), lightDirection), 0.0);
    }
    outColor = vec4(fragmentColor * lighting, 1.0);
}
