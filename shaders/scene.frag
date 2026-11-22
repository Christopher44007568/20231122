#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec4 bg;
    vec4 accent;
    vec4 line;
    uvec4 digest;
    uint badge;
    uint stripes;
    uint submitCount;
    uint titleSerial;
} pc;

float box(vec2 p, vec2 center, vec2 halfSize) {
    vec2 d = abs(p - center) - halfSize;
    return step(max(d.x, d.y), 0.0);
}

uint bitAt(uint index) {
    uint word = index >> 5u;
    uint bit = index & 31u;
    return (pc.digest[word] >> bit) & 1u;
}

void main() {
    vec2 uv = clamp(vUv, vec2(0.0), vec2(1.0));
    vec3 color = pc.bg.rgb;

    vec2 grid = abs(fract(uv * vec2(16.0, 10.0)) - 0.5);
    float gridLine = step(0.482, max(grid.x, grid.y));
    color = mix(color, pc.line.rgb, gridLine * 0.18);

    for (uint i = 0u; i < 4u; ++i) {
        if (i < pc.stripes) {
            float x = 0.14 + float(i) * 0.075;
            float bar = box(uv, vec2(x, 0.58), vec2(0.018, 0.22));
            color = mix(color, pc.accent.rgb, bar * 0.90);
        }
    }

    for (uint i = 0u; i < 4u; ++i) {
        if (i < pc.badge) {
            vec2 center = vec2(0.24 + float(i) * 0.11, 0.22);
            float cell = box(uv, center, vec2(0.045, 0.075));
            color = mix(color, pc.accent.rgb, cell);
            float inner = box(uv, center, vec2(0.023, 0.042));
            color = mix(color, pc.bg.rgb, inner * 0.75);
        }
    }

    for (uint i = 0u; i < 4u; ++i) {
        if (i < pc.submitCount) {
            float x = 0.78 + float(i) * 0.045;
            float tick = box(uv, vec2(x, 0.86), vec2(0.010, 0.055));
            color = mix(color, pc.accent.rgb, tick);
        }
    }

    if (pc.titleSerial > 0u) {
        float diamond = step(abs(uv.x - 0.90) + abs(uv.y - 0.72), 0.028);
        color = mix(color, pc.accent.rgb, diamond);
    }

    vec2 cell = floor((uv - vec2(0.64, 0.08)) / vec2(0.020, 0.036));
    bool inDigest = uv.x >= 0.64 && uv.x < 0.96 && uv.y >= 0.08 && uv.y < 0.44;
    if (inDigest) {
        uint index = uint(cell.y) * 16u + uint(cell.x);
        uint bit = bitAt(index % 128u);
        vec2 local = fract((uv - vec2(0.64, 0.08)) / vec2(0.020, 0.036));
        float fill = step(local.x, 0.72) * step(local.y, 0.72);
        color = mix(color, bit == 1u ? pc.accent.rgb : pc.line.rgb, fill);
    }

    float border = 1.0 - box(uv, vec2(0.5), vec2(0.492));
    color = mix(color, pc.line.rgb, border * 0.55);
    outColor = vec4(color, 1.0);
}
