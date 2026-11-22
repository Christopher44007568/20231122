#version 450

layout(push_constant) uniform Push {
  vec4 rect;
  vec4 params;  // x = texture array layer, y = placeholder flag
} pc;

layout(set = 0, binding = 0) uniform sampler2DArray texArray;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main() {
  if (pc.params.y > 0.5) {
    // Emulated "page not resident yet" placeholder: magenta checkerboard.
    vec2 g = floor(uv * 8.0);
    float c = mod(g.x + g.y, 2.0);
    outColor = mix(vec4(0.1, 0.0, 0.1, 1.0), vec4(1.0, 0.0, 1.0, 1.0), c);
    return;
  }
  outColor = texture(texArray, vec3(uv, pc.params.x));
}
