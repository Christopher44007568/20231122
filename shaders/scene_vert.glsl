#version 450
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;
layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 tint;      // rgb multiplier, a = useReflection flag
  vec4 params;    // x = mirror (1.0 flips y), y/z/w unused
} pc;
layout(location = 0) out vec3 vColor;
layout(location = 1) out vec2 vUv;
void main() {
  vec4 p = pc.mvp * vec4(inPos, 0.0, 1.0);
  p.y *= pc.params.x;
  gl_Position = p;
  vColor = inColor * pc.tint.rgb;
  vUv = inPos * 0.5 + 0.5;
}
