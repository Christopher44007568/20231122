#version 450

layout(binding = 0) uniform FrameUBO {
  mat4 view;
  mat4 proj;
  mat4 lightVP[3];
  vec4 splits;
  vec4 lightDir;
  vec4 camPos;
} ubo;

layout(push_constant) uniform PC {
  mat4 model;
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vColor;
layout(location = 3) out float vViewDepth;

void main() {
  vec4 world = pc.model * vec4(inPos, 1.0);
  vWorldPos = world.xyz;
  vNormal = mat3(pc.model) * inNormal;
  vColor = inColor;
  vec4 viewPos = ubo.view * world;
  vViewDepth = -viewPos.z;
  gl_Position = ubo.proj * viewPos;
}
