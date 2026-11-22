#version 450

layout(binding = 0) uniform sampler2D albedo_texture;
layout(binding = 1) uniform sampler2D normal_texture;
layout(binding = 2) uniform sampler2D position_texture;

layout(location = 0) out vec4 out_color;

void main() {
  const ivec2 pixel = ivec2(gl_FragCoord.xy);
  const vec4 albedo = texelFetch(albedo_texture, pixel, 0);
  const vec4 encoded_normal = texelFetch(normal_texture, pixel, 0);
  const vec4 world_position = texelFetch(position_texture, pixel, 0);

  if (world_position.a < 0.5) {
    const float vignette = 0.035 - 0.02 * length(gl_FragCoord.xy / vec2(640.0, 360.0) - 0.5);
    out_color = vec4(vec3(vignette), 1.0);
    return;
  }

  const vec3 normal = normalize(encoded_normal.xyz * 2.0 - 1.0);
  const vec3 light_direction = normalize(vec3(-0.35, 0.75, 0.6));
  const vec3 view_direction = normalize(vec3(0.0, 0.0, 2.6) - world_position.xyz);
  const vec3 half_direction = normalize(light_direction + view_direction);
  const float roughness = max(albedo.a, 0.04);
  const float metallic = encoded_normal.a;
  const float n_dot_l = max(dot(normal, light_direction), 0.0);
  const float n_dot_h = max(dot(normal, half_direction), 0.0);
  const vec3 diffuse = albedo.rgb * (0.18 + n_dot_l * (1.0 - metallic));
  const vec3 specular_color = mix(vec3(0.04), albedo.rgb, metallic);
  const float specular_power = mix(96.0, 8.0, roughness);
  const vec3 specular =
      specular_color * pow(n_dot_h, specular_power) * (1.0 - roughness * 0.6);
  const vec3 rim =
      vec3(0.08, 0.12, 0.20) * pow(1.0 - max(dot(normal, view_direction), 0.0), 3.0);
  const vec3 linear = diffuse + specular + rim;
  out_color = vec4(pow(clamp(linear, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
