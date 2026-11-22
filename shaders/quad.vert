#version 450

layout(location = 0) out vec2 uv;

void main() {
  // Fullscreen triangle from gl_VertexIndex, no vertex buffers needed.
  vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  uv = pos;
  gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
