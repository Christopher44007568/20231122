#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in uint inSourceOffset;

layout(location = 0) out vec3 vColor;
layout(location = 1) flat out uint vSourceOffset;
layout(location = 2) flat out uint vPrimitiveId;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    vColor = inColor;
    vSourceOffset = inSourceOffset;
    vPrimitiveId = uint(gl_VertexIndex) / 3u;
}
