#version 450

layout(set = 0, binding = 0) readonly buffer SourcePixels {
    uint pixels[];
} source_buffer;

layout(push_constant) uniform Parameters {
    uint width;
    uint height;
} params;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main() {
    ivec2 pixel = ivec2(clamp(uv, vec2(0.0), vec2(0.999999)) * vec2(params.width, params.height));
    uint packed = source_buffer.pixels[pixel.y * int(params.width) + pixel.x];
    uvec3 bytes = uvec3(
        packed & 255u,
        (packed >> 8u) & 255u,
        (packed >> 16u) & 255u);
    outColor = vec4(vec3(bytes) / 255.0, 1.0);
}

