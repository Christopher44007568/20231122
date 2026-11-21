#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in uvec4 inBoneIndices;
layout(location = 4) in vec4 inBoneWeights;

layout(set = 0, binding = 0) uniform SkinMatrices {
    mat4 skin[64];
} skinUbo;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUv;
layout(location = 2) out float outWeightCoverage;

void main() {
    mat4 skinMatrix = inBoneWeights.x * skinUbo.skin[inBoneIndices.x]
                    + inBoneWeights.y * skinUbo.skin[inBoneIndices.y]
                    + inBoneWeights.z * skinUbo.skin[inBoneIndices.z]
                    + inBoneWeights.w * skinUbo.skin[inBoneIndices.w];

    vec4 skinnedPosition = skinMatrix * vec4(inPosition, 1.0);
    vec3 skinnedNormal = mat3(skinMatrix) * inNormal;

    gl_Position = pc.viewProj * skinnedPosition;
    outNormal = normalize(skinnedNormal);
    outUv = inUv;
    outWeightCoverage = inBoneWeights.x + inBoneWeights.y
                      + inBoneWeights.z + inBoneWeights.w;
}
