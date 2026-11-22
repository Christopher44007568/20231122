#version 450

layout(triangles) in;
layout(line_strip, max_vertices = 6) out;

layout(location = 0) in vec3 vColor[];
layout(location = 1) flat in uint vSourceOffset[];
layout(location = 2) flat in uint vPrimitiveId[];

layout(location = 0) out vec3 outColor;
layout(location = 1) flat out uint outSourceOffset;
layout(location = 2) flat out uint outPrimitive;
layout(location = 3) flat out uint outEdge;

struct Record {
    vec4 p0;
    vec4 p1;
    vec4 color;
    uvec4 meta;
};

layout(std430, binding = 0) buffer Capture {
    Record records[];
};

void main() {
    vec3 center = (gl_in[0].gl_Position.xyz +
                   gl_in[1].gl_Position.xyz +
                   gl_in[2].gl_Position.xyz) / 3.0;
    vec3 position0 = gl_in[0].gl_Position.xyz;
    vec3 position1 = gl_in[1].gl_Position.xyz;
    vec3 position2 = gl_in[2].gl_Position.xyz;
    vec3 edge0_start = center + (position0 - center) * 0.96;
    vec3 edge0_end = center + (position1 - center) * 0.96;
    vec3 edge1_start = center + (position1 - center) * 0.96;
    vec3 edge1_end = center + (position2 - center) * 0.96;
    vec3 edge2_start = center + (position2 - center) * 0.96;
    vec3 edge2_end = center + (position0 - center) * 0.96;
    vec4 color0 = vec4((vColor[0] + vColor[1]) * 0.5, 1.0);
    vec4 color1 = vec4((vColor[1] + vColor[2]) * 0.5, 1.0);
    vec4 color2 = vec4((vColor[2] + vColor[0]) * 0.5, 1.0);
    uint offset0 = min(vSourceOffset[0], vSourceOffset[1]);
    uint offset1 = min(vSourceOffset[1], vSourceOffset[2]);
    uint offset2 = min(vSourceOffset[2], vSourceOffset[0]);
    uint primitive = vPrimitiveId[0];

    outColor = color0.rgb;
    outSourceOffset = offset0;
    outPrimitive = primitive;
    outEdge = 0u;
    gl_Position = vec4(edge0_start, 1.0);
    EmitVertex();
    gl_Position = vec4(edge0_end, 1.0);
    EmitVertex();
    EndPrimitive();

    outColor = color1.rgb;
    outSourceOffset = offset1;
    outEdge = 1u;
    gl_Position = vec4(edge1_start, 1.0);
    EmitVertex();
    gl_Position = vec4(edge1_end, 1.0);
    EmitVertex();
    EndPrimitive();

    outColor = color2.rgb;
    outSourceOffset = offset2;
    outEdge = 2u;
    gl_Position = vec4(edge2_start, 1.0);
    EmitVertex();
    gl_Position = vec4(edge2_end, 1.0);
    EndPrimitive();

    uint base = primitive * 3u;
    records[base].p0 = vec4(edge0_start, 1.0);
    records[base].p1 = vec4(edge0_end, 1.0);
    records[base].color = color0;
    records[base].meta = uvec4(offset0, primitive, 0u, 0u);
    records[base + 1u].p0 = vec4(edge1_start, 1.0);
    records[base + 1u].p1 = vec4(edge1_end, 1.0);
    records[base + 1u].color = color1;
    records[base + 1u].meta = uvec4(offset1, primitive, 1u, 0u);
    records[base + 2u].p0 = vec4(edge2_start, 1.0);
    records[base + 2u].p1 = vec4(edge2_end, 1.0);
    records[base + 2u].color = color2;
    records[base + 2u].meta = uvec4(offset2, primitive, 2u, 0u);
}
