#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec2 outUv;
layout(location = 1) flat out vec3 outBaseColor;
layout(location = 2) flat out float outBorderPhase;

layout(push_constant) uniform PushConstants {
  mat4 viewProjection;
  vec4 baseColor;
  vec4 geometry;
  vec4 identity;
} pc;

void main() {
  vec2 world = pc.geometry.xy + inPosition * pc.geometry.zw;
  gl_Position = pc.viewProjection * vec4(world, pc.identity.x, 1.0);
  outUv = inUv;
  outBaseColor = pc.baseColor.rgb;
  outBorderPhase = pc.identity.z;
}

