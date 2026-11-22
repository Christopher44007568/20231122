#version 450
layout(location = 0) in vec3 in_pos;
layout(location = 1) in uint in_flags;
layout(push_constant) uniform PC { mat4 mvp; } pc;
layout(location = 0) out vec3 v_color;

void main() {
  gl_Position = pc.mvp * vec4(in_pos, 1.0);
  gl_PointSize = 2.0;
  v_color = (in_flags & 1u) != 0u ? vec3(1.0, 0.25, 0.2)
                                  : vec3(0.55, 0.75, 1.0);
}
