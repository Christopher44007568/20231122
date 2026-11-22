#version 450
// draw#0：全屏背景三角形，输出清屏色

layout(push_constant, std140) uniform PushConstants {
    layout(offset = 0)  vec4 viewport;
    layout(offset = 16) vec4 grid;
    layout(offset = 32) vec4 color;
    layout(offset = 48) vec4 clearColor;
} pc;

layout(location = 0) flat out vec4 vColor;

void main() {
    vColor = pc.clearColor;
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
