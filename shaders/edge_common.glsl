// Shared screen-space edge metric. G-buffer: rgb = view-space normal, a = view depth.
#ifndef EDGE_COMMON_GLSL
#define EDGE_COMMON_GLSL

struct EdgeSample {
    float depthDiff;   // max relative depth discontinuity over 4 neighbours
    float normalDiff;  // max (1 - dot) over 4 neighbours
    float depthGT;     // ground-truth edge strength: max |2nd difference of 1/z|
    vec4 dz;           // per-neighbour relative depth diffs  (L, R, D, U)
    vec4 dn;           // per-neighbour normal diffs        (L, R, D, U)
};

EdgeSample computeEdge(sampler2D gbuf, vec2 uv, vec2 texel) {
    EdgeSample r;
    vec4 c = texture(gbuf, uv);
    float zc = c.a;
    vec3 nc = c.rgb;
    bool cValid = dot(nc, nc) > 1e-8;

    vec2 offs[4] = vec2[4](vec2(-1, 0), vec2(1, 0), vec2(0, -1), vec2(0, 1));
    r.depthDiff = 0.0;
    r.normalDiff = 0.0;
    float lc = 1.0 / max(zc, 1e-3);
    float l[4];
    for (int i = 0; i < 4; ++i) {
        vec4 s = texture(gbuf, uv + offs[i] * texel);
        float zs = s.a;
        vec3 ns = s.rgb;
        bool sValid = dot(ns, ns) > 1e-8;
        l[i] = 1.0 / max(zs, 1e-3);
        float dz = abs(zs - zc) / max(zc, 1e-3);
        float dn = (cValid && sValid) ? (1.0 - clamp(dot(normalize(nc), normalize(ns)), -1.0, 1.0)) : 0.0;
        r.dz[i] = dz;
        r.dn[i] = dn;
        r.depthDiff = max(r.depthDiff, dz);
        r.normalDiff = max(r.normalDiff, dn);
    }
    // 1/viewZ is linear in screen space on planar surfaces, so its second
    // difference vanishes on slopes and spikes only at real discontinuities.
    float ddx = abs(l[0] - 2.0 * lc + l[1]);
    float ddy = abs(l[2] - 2.0 * lc + l[3]);
    r.depthGT = max(ddx, ddy);
    return r;
}

#endif
