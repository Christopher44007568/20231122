#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 instanceColumn0;
layout(location = 3) in vec4 instanceColumn1;
layout(location = 4) in vec4 instanceColumn2;
layout(location = 5) in vec4 instanceColumn3;
layout(location = 6) in vec4 instanceColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
} pc;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec4 outColor;

void main() {
    mat4 model = mat4(instanceColumn0, instanceColumn1,
                      instanceColumn2, instanceColumn3);
    vec4 world = model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProjection * world;
    outNormal = normalize(mat3(model) * inNormal);
    outColor = instanceColor;
}
