#version 450

layout(location = 0) in vec3 vNormalV;
layout(location = 1) in float vViewZ;
layout(location = 2) flat in uint vObjectId;

layout(location = 0) out vec4 outGbuffer;
layout(location = 1) out uint outObjectId;

void main() {
    outGbuffer = vec4(normalize(vNormalV), max(vViewZ, 0.0));
    outObjectId = vObjectId;
}
