#version 450

layout(location = 0) in vec4 inPos;
layout(location = 0) out vec4 vPos;

void main()
{
    vPos = inPos;
    gl_Position = inPos;
}
