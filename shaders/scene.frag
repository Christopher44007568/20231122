#version 450

layout(push_constant) uniform PushConstants {
    vec2 resolution;
    float scene_time_sec;
    float object_period_sec;
    float object_half_extent;
    uint probe_count;
    uint device_index;
    uint reserved0;
    uint reserved1;
} pc;

layout(location = 0) out vec4 out_color;

const vec3 OBJECT_COLORS[4] = vec3[4](
    vec3(230.0, 60.0, 50.0) / 255.0,
    vec3(60.0, 210.0, 110.0) / 255.0,
    vec3(70.0, 120.0, 235.0) / 255.0,
    vec3(235.0, 195.0, 55.0) / 255.0
);

void main() {
    vec2 pixel = gl_FragCoord.xy / pc.resolution;
    int hit = -1;

    for (int i = 0; i < 4; ++i) {
        float phase = fract(pc.scene_time_sec / pc.object_period_sec);
        float angle = 6.283185307179586 * (phase + float(i) * 0.25);
        vec2 center = vec2(0.5 + 0.22 * cos(angle),
                           0.5 + 0.22 * sin(angle));
        if (abs(pixel.x - center.x) <= pc.object_half_extent &&
            abs(pixel.y - center.y) <= pc.object_half_extent) {
            hit = i;
        }
    }

    vec3 color = vec3(24.0, 28.0, 42.0) / 255.0;
    ivec2 checker = ivec2(floor(pixel * vec2(16.0, 10.0)));
    if (((checker.x + checker.y) & 1) == 0) {
        color = vec3(32.0, 36.0, 54.0) / 255.0;
    }
    if (hit >= 0) {
        color = OBJECT_COLORS[hit];
    }
    out_color = vec4(color, 1.0);
}
