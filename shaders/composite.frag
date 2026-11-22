#version 450

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D baselineColor;
layout(set = 0, binding = 1) uniform sampler2D baselineAux;
layout(set = 0, binding = 2) uniform sampler2D currentColor;
layout(set = 0, binding = 3) uniform sampler2D currentAux;

layout(push_constant) uniform PushConstants {
  vec4 controls;
} pc;

void main() {
  vec2 sceneSize = vec2(textureSize(baselineColor, 0));
  vec2 uv = vec2(gl_FragCoord.x / sceneSize.x,
                 1.0 - gl_FragCoord.y / sceneSize.y);
  vec3 base = texture(baselineColor, uv).rgb;
  vec3 current = texture(currentColor, uv).rgb;
  float baseId = texture(baselineAux, uv).y;
  float currentId = texture(currentAux, uv).y;
  float focusId = pc.controls.y;
  int mode = int(pc.controls.x + 0.5);
  float split = pc.controls.z;
  float size = sceneSize.x;
  float splitPixels = split * size;
  vec3 color = current;

  if (mode == 0) {
    color = base;
  } else if (mode == 2) {
    color = gl_FragCoord.x < splitPixels ? base : current;
    if (abs(gl_FragCoord.x - splitPixels) < 1.5) {
      color = vec3(0.02, 0.86, 0.78);
    }
  } else if (mode == 3) {
    bool focusBefore = abs(baseId - focusId) < 0.5;
    bool focusAfter = abs(currentId - focusId) < 0.5;
    vec3 dimBase = mix(vec3(0.06, 0.08, 0.10), base * 0.32, 0.40);
    color = dimBase;
    if (focusBefore && !focusAfter) {
      color = vec3(0.96, 0.20, 0.26);
    } else if (!focusBefore && focusAfter) {
      color = vec3(0.14, 0.88, 0.50);
    } else if (focusBefore && focusAfter) {
      color = mix(dimBase, vec3(0.30, 0.56, 0.76), 0.58);
    }
    float border = min(gl_FragCoord.x, gl_FragCoord.y);
    if (border < 2.0) {
      color = vec3(0.02, 0.86, 0.78);
    }
  }
  outColor = vec4(color, 1.0);
}

