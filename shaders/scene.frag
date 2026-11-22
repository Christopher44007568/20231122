#version 450

layout(location = 0) in vec2 inUv;
layout(location = 1) flat in vec3 inBaseColor;
layout(location = 2) flat in float inBorderPhase;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outAux;

layout(push_constant) uniform PushConstants {
  mat4 viewProjection;
  vec4 baseColor;
  vec4 geometry;
  vec4 identity;
} pc;

void main() {
  vec2 edgeDistance = 1.0 - abs(inUv);
  float edge = min(edgeDistance.x, edgeDistance.y);
  float border = 1.0 - smoothstep(0.012, 0.035, edge);
  float microGrid =
    0.5 + 0.5 * sin((inUv.x + inBorderPhase) * 34.0) *
              sin((inUv.y - inBorderPhase) * 27.0);
  vec3 face = inBaseColor * (0.70 + 0.22 * microGrid);
  vec3 color = mix(face, vec3(0.96, 0.97, 0.99), border * 0.88);
  float depthOffsetUnits = pc.identity.w;
  gl_FragDepth = clamp(gl_FragCoord.z + depthOffsetUnits / 8388608.0,
                       0.0, 1.0);
  outColor = vec4(color, 1.0);
  outAux = vec2(gl_FragDepth, pc.identity.y);
}

