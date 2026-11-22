#version 450

layout(push_constant) uniform Push {
  vec4 rect;    // x, y, w, h in pixels
  vec4 params;  // x = texture array layer
} pc;

layout(location = 0) out vec2 uv;

const vec2 kPos[6] = vec2[](
  vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
  vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

layout(constant_id = 0) const float kViewportW = 512.0;
layout(constant_id = 1) const float kViewportH = 512.0;

void main() {
  vec2 p = kPos[gl_VertexIndex];
  uv = p;
  vec2 px = pc.rect.xy + p * pc.rect.zw;
  vec2 ndc = vec2(px.x / kViewportW * 2.0 - 1.0,
                  px.y / kViewportH * 2.0 - 1.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
}
