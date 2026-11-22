#version 450

layout(location = 0) in vec2 FragUV;
layout(location = 1) in vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D FontTexture;

layout(location = 0) out vec4 OutColor;

void main() {
    OutColor = FragColor * texture(FontTexture, FragUV);
}
