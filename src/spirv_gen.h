#pragma once

#include <cstdint>
#include <vector>

// The environment this tool runs in has no GLSL compiler, so the handful of
// shaders the analyzer needs are emitted directly as SPIR-V. Each generator
// returns a complete, valid SPIR-V module (Vulkan environment).

// Depth-pass vertex shader:
//   layout(push_constant) uniform Push { mat4 mvp; } pc;
//   layout(location=0) in vec3 inPos;
//   gl_Position = pc.mvp * vec4(inPos, 1);
std::vector<uint32_t> genDepthVert();

// Depth-pass fragment shader: empty (depth-only rendering).
std::vector<uint32_t> genDepthFrag();

// Fullscreen-triangle vertex shader for the depth-map viewer:
//   layout(location=0) out vec2 uv;  (uses gl_VertexIndex)
std::vector<uint32_t> genBlitVert();

// Viewer fragment shader:
//   layout(set=0,binding=0) uniform sampler2D tex;
//   layout(location=0) in vec2 uv; layout(location=0) out vec4 c;
//   c = vec4(vec3(texture(tex, uv).r), 1);
std::vector<uint32_t> genBlitFrag();
