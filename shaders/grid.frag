#version 450

layout(push_constant, std140) uniform PushConstants {
    layout(offset = 0)  vec4 viewport;
    layout(offset = 16) vec4 grid;
    layout(offset = 32) vec4 color;
    layout(offset = 48) vec4 clearColor;
} pc;

layout(location = 0) out vec4 outColor;
layout(location = 0) flat in vec4 vColor;

void main() {
    outColor = vColor;
}
