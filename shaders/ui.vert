#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    vec2 screenSize;
} pc;

layout(location = 0) out vec4 fragColor;

void main() {
    vec2 clip = inPosition / pc.screenSize * 2.0 - 1.0;
    gl_Position = vec4(clip, 0.0, 1.0);
    fragColor = inColor;
}
