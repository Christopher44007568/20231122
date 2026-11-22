#version 450

layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 UV;
layout(location = 2) in vec4 Color;

layout(push_constant) uniform PushConstants {
    vec2 Scale;
    vec2 Translate;
} pc;

layout(location = 0) out vec2 FragUV;
layout(location = 1) out vec4 FragColor;

void main() {
    FragUV = UV;
    FragColor = Color;
    gl_Position = vec4(Position * pc.Scale + pc.Translate, 0.0, 1.0);
}
