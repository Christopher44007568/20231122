#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} pc;

layout(location = 0) out vec3 fragmentColor;
layout(location = 1) out vec3 fragmentNormal;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragmentColor = inColor;
    fragmentNormal = inNormal;
}
