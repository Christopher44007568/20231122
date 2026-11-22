#version 450

struct Record {
    vec4 p0;
    vec4 p1;
    vec4 color;
    uvec4 meta;
};

layout(std430, binding = 0) readonly buffer Capture {
    Record records[];
};

layout(location = 0) out vec3 outColor;

void main() {
    uint record_index = uint(gl_VertexIndex) / 2u;
    bool is_second = (uint(gl_VertexIndex) & 1u) != 0u;
    Record record = records[record_index];
    gl_Position = is_second ? record.p1 : record.p0;
    outColor = record.color.rgb;
}
