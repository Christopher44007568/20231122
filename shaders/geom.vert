#version 450

layout(push_constant) uniform PC {
    mat4 modelView;
    mat4 proj;
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 vNormalV;
layout(location = 1) out float vViewZ;
layout(location = 2) flat out uint vObjectId;

void main() {
    vec4 posV = pc.modelView * vec4(inPos, 1.0);
    vNormalV = mat3(pc.modelView) * inNormal;
    vViewZ = -posV.z;
    vObjectId = uint(gl_InstanceIndex);
    gl_Position = pc.proj * posV;
}
