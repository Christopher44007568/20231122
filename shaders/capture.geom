#version 450

// TOPO: 0 = points, 1 = line_strip, 2 = triangle_strip
#ifndef TOPO
#define TOPO 0
#endif
#ifndef MAX_VERTS
#define MAX_VERTS 8
#endif
#ifndef INVOCATIONS
#define INVOCATIONS 4
#endif

#if TOPO == 0
layout(points, invocations = INVOCATIONS) in;
layout(points, max_vertices = MAX_VERTS) out;
#elif TOPO == 1
layout(lines, invocations = INVOCATIONS) in;
layout(line_strip, max_vertices = MAX_VERTS) out;
#else
layout(triangles, invocations = INVOCATIONS) in;
layout(triangle_strip, max_vertices = MAX_VERTS) out;
#endif

layout(location = 0) in vec4 vPos[];

struct VertexRec
{
    uint invocation;
    uint seq;        // emit sequence index inside this invocation
    uint inputVerts; // gl_in.length() seen by this invocation
    uint pad;
    vec4 pos;
};

layout(std430, binding = 0) buffer CaptureBuf
{
    uint emittedTotal;   // total EmitVertex calls that reached the shader
    uint droppedTotal;   // records that did not fit in the capture buffer
    uint firstDropInv;   // invocation of first dropped record (0xffffffff = none)
    uint capacity;       // record capacity, filled by host
    VertexRec recs[];
} cap;

layout(push_constant) uniform PC
{
    uint emitCount;      // requested EmitVertex calls per invocation
    uint inspectOnly;    // 0 = all invocations, 1 = only inspectInvocation
    uint inspectInvocation;
    uint maxVertices;    // host-side view of the compiled max_vertices
} pc;

void main()
{
    if (pc.inspectOnly != 0 && gl_InvocationID != pc.inspectInvocation)
        return;

    for (uint i = 0; i < pc.emitCount; ++i)
    {
        uint slot = atomicAdd(cap.emittedTotal, 1u);
        if (slot < cap.capacity)
        {
            cap.recs[slot].invocation = uint(gl_InvocationID);
            cap.recs[slot].seq = i;
            cap.recs[slot].inputVerts = uint(gl_in.length());
            cap.recs[slot].pad = 0u;
            vec4 base = vPos[0];
            cap.recs[slot].pos = vec4(base.x + float(i), base.y,
                                     base.z + float(gl_InvocationID), 1.0);
        }
        else
        {
            atomicAdd(cap.droppedTotal, 1u);
            atomicMin(cap.firstDropInv, uint(gl_InvocationID));
        }
        gl_Position = vPos[0];
        EmitVertex();
#if TOPO == 0
        EndPrimitive(); // points: one primitive per vertex
#endif
    }
#if TOPO != 0
    EndPrimitive(); // strips: single strip per invocation
#endif
}
