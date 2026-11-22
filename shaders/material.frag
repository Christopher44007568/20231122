#version 450

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_world_position;
layout(location = 2) flat in uint in_material_index;

layout(location = 0) out vec4 out_color;

struct GpuMaterial {
  vec4 base_roughness;
  vec4 properties;
};

layout(set = 0, binding = 0, std430) readonly buffer MaterialBuffer {
  GpuMaterial materials[];
};

layout(set = 0, binding = 1) uniform sampler2D reflection_sphere;

const float kPi = 3.14159265359;

vec2 direction_to_uv(vec3 direction) {
  vec3 d = normalize(direction);
  return vec2(atan(d.z, d.x) / (2.0 * kPi) + 0.5,
              asin(clamp(d.y, -1.0, 1.0)) / kPi + 0.5);
}

void main() {
  GpuMaterial material = materials[in_material_index];
  vec3 base_color = material.base_roughness.xyz;
  float roughness = clamp(material.base_roughness.w, 0.03, 1.0);
  float metallic = clamp(material.properties.x, 0.0, 1.0);
  float reflectance = clamp(material.properties.y, 0.0, 1.0);
  float energy_integral = clamp(material.properties.z, 0.0, 1.0);
  float albedo_scale = clamp(material.properties.w, 0.0, 8.0);

  vec3 normal = normalize(in_normal);
  vec3 view_direction = vec3(0.0, 0.0, 1.0);
  vec3 light_direction = normalize(vec3(-0.35, 0.65, 0.72));
  vec3 half_direction = normalize(light_direction + view_direction);
  float n_dot_l = max(dot(normal, light_direction), 0.0);
  float n_dot_h = max(dot(normal, half_direction), 0.0);
  float n_dot_v = max(dot(normal, view_direction), 0.0);
  float fresnel = reflectance +
                  (1.0 - reflectance) * pow(1.0 - n_dot_v, 5.0);

  vec3 reflected = reflect(-view_direction, normal);
  vec3 sample_direction = normalize(mix(reflected, normal, roughness * 0.72));
  vec3 environment =
      texture(reflection_sphere, direction_to_uv(sample_direction)).rgb;

  vec3 diffuse = base_color * albedo_scale * (1.0 - metallic) *
                 (0.18 + 0.62 * n_dot_l);
  float specular_power = mix(96.0, 4.0, roughness);
  float specular_weight = pow(n_dot_h, specular_power) *
                          mix(0.10, 1.10, metallic) * fresnel;
  vec3 specular =
      mix(vec3(1.0), base_color, metallic) * specular_weight;
  vec3 reflected_light = environment * fresnel *
                         mix(0.35, 1.15, metallic) *
                         (1.0 - roughness * 0.55);
  float energy_gain = mix(0.55, 1.0, energy_integral);
  vec3 linear_color = (diffuse + specular + reflected_light) * energy_gain;
  vec3 display_color = pow(clamp(linear_color, 0.0, 1.0), vec3(1.0 / 2.2));
  out_color = vec4(display_color, 1.0);
}
