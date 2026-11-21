#version 450

layout(location = 0) in vec3 vNormalV;
layout(location = 1) in float vViewZ;

layout(location = 0) out vec4 outGbuf; // rgb = view-space normal, a = view-space depth

void main() {
    outGbuf = vec4(normalize(vNormalV), vViewZ);
}
