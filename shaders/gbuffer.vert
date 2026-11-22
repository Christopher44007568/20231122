#version 450

layout(push_constant) uniform PushConstants {
  mat4 view_projection;
  vec4 albedo;
  vec4 params;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec3 v_world_position;
layout(location = 1) out vec3 v_world_normal;
flat layout(location = 2) out uint v_object_id;

void main() {
  gl_Position = pc.view_projection * vec4(in_position, 1.0);
  v_world_position = in_position;
  v_world_normal = normalize(in_normal);
  v_object_id = uint(pc.params.z + 0.5);
}
