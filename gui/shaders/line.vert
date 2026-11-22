#version 450

layout(push_constant) uniform PushConstants {
    mat4 view_proj;
    vec4 color;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 0) out vec4 out_color;

void main() {
    gl_Position = pc.view_proj * vec4(in_position, 1.0);
    out_color = pc.color;
}
