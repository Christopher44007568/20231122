#version 450
layout(set = 0, binding = 0) uniform sampler2D reflectionTex;
layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main() {
  // Reflection pass already renders mirrored, so sample at the same uv.
  vec3 refl = texture(reflectionTex, vUv).rgb;
  outColor = vec4(mix(vColor, refl, 0.55), 1.0);
}
