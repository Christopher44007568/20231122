#version 450

layout(binding = 0) uniform sampler2D fog_image;
layout(location = 0) out vec4 out_color;

void main()
{
    ivec2 size = textureSize(fog_image, 0);
    vec2 uv = gl_FragCoord.xy / vec2(size);
    vec3 color = texture(fog_image, uv).rgb;
    color = vec3(1.0) - exp(-color * 0.85);
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    out_color = vec4(color, 1.0);
}
