#version 450

layout(binding = 0) uniform FrameUBO {
  mat4 view;
  mat4 proj;
  mat4 lightVP[3];
  vec4 splits;
  vec4 lightDir;
  vec4 camPos;
} ubo;

layout(binding = 1) uniform sampler2DShadow shadowMap0;
layout(binding = 2) uniform sampler2DShadow shadowMap1;
layout(binding = 3) uniform sampler2DShadow shadowMap2;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vColor;
layout(location = 3) in float vViewDepth;

layout(location = 0) out vec4 outColor;

const vec3 CASCADE_TINT[3] = vec3[3](
  vec3(1.0, 0.35, 0.35),
  vec3(0.35, 1.0, 0.35),
  vec3(0.35, 0.55, 1.0)
);

float sampleShadow(int cascade, vec3 worldPos) {
  vec4 ls;
  if (cascade == 0) {
    ls = ubo.lightVP[0] * vec4(worldPos, 1.0);
    vec3 p = vec3(ls.xy / ls.w * 0.5 + 0.5, ls.z / ls.w);
    if (any(lessThan(p.xy, vec2(0.0))) || any(greaterThan(p.xy, vec2(1.0)))) return 1.0;
    return texture(shadowMap0, vec3(p.xy, p.z));
  } else if (cascade == 1) {
    ls = ubo.lightVP[1] * vec4(worldPos, 1.0);
    vec3 p = vec3(ls.xy / ls.w * 0.5 + 0.5, ls.z / ls.w);
    if (any(lessThan(p.xy, vec2(0.0))) || any(greaterThan(p.xy, vec2(1.0)))) return 1.0;
    return texture(shadowMap1, vec3(p.xy, p.z));
  }
  ls = ubo.lightVP[2] * vec4(worldPos, 1.0);
  vec3 p = vec3(ls.xy / ls.w * 0.5 + 0.5, ls.z / ls.w);
  if (any(lessThan(p.xy, vec2(0.0))) || any(greaterThan(p.xy, vec2(1.0)))) return 1.0;
  return texture(shadowMap2, vec3(p.xy, p.z));
}

void main() {
  int cascade = 2;
  if (vViewDepth <= ubo.splits.x) cascade = 0;
  else if (vViewDepth <= ubo.splits.y) cascade = 1;

  vec3 N = normalize(vNormal);
  vec3 L = normalize(-ubo.lightDir.xyz);
  float ndl = max(dot(N, L), 0.0);
  float shadow = sampleShadow(cascade, vWorldPos);

  vec3 ambient = 0.18 * vColor;
  vec3 diffuse = ndl * shadow * vColor;
  vec3 lit = ambient + diffuse;

  // Cascade visualization: blend tint over lighting.
  vec3 vis = mix(lit, CASCADE_TINT[cascade] * (0.35 + 0.65 * shadow), 0.45);
  outColor = vec4(vis, 1.0);
}
