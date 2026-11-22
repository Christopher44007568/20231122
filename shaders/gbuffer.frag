#version 450

layout(push_constant) uniform PushConstants {
  mat4 view_projection;
  vec4 albedo;
  vec4 params;
} pc;

layout(location = 0) in vec3 v_world_position;
layout(location = 1) in vec3 v_world_normal;
flat layout(location = 2) in uint v_object_id;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_position;
layout(location = 3) out uvec4 out_object_id;

void main() {
  out_albedo = vec4(pc.albedo.rgb, clamp(pc.params.x, 0.0, 1.0));
  out_normal = vec4(normalize(v_world_normal) * 0.5 + 0.5,
                    clamp(pc.params.y, 0.0, 1.0));
  out_position = vec4(v_world_position, 1.0);
  out_object_id = uvec4(v_object_id, 0u, 0u, 0u);
}
