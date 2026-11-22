#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_world_position;
layout(location = 2) flat out uint out_material_index;

struct Instance {
  vec3 position;
  uint material_index;
};

struct GpuMaterial {
  vec4 base_roughness;
  vec4 properties;
};

layout(set = 0, binding = 0, std430) readonly buffer MaterialBuffer {
  GpuMaterial materials[];
};

layout(set = 0, binding = 2, std430) readonly buffer InstanceBuffer {
  Instance instances[];
};

layout(push_constant) uniform PushConstants {
  vec2 half_extent;
} push;

void main() {
  Instance instance = instances[gl_InstanceIndex];
  vec3 world = instance.position + in_position;
  out_normal = in_normal;
  out_world_position = world;
  out_material_index = instance.material_index;
  gl_Position = vec4(world.x / push.half_extent.x,
                     world.y / push.half_extent.y,
                     0.5 - world.z * 0.20, 1.0);
}
