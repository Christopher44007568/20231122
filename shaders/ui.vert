#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
  vec2 screenSize;
} pc;

void main() {
  vec2 ndc = vec2(inPosition.x / pc.screenSize.x * 2.0 - 1.0,
                  1.0 - inPosition.y / pc.screenSize.y * 2.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
  outColor = inColor;
}

