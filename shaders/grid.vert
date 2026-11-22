#version 450
// draw#1：vkCmdDraw(8, cols*rows, 0, 0)，LINE_LIST
// 每个单元实例枚举 4 条边（8 顶点），非轮廓边退化为零长度线段

// 全部使用 16 字节对齐的 vec4，消除 std140 混合对齐歧义，与 C++ PushData 一一对应
layout(push_constant, std140) uniform PushConstants {
    layout(offset = 0)  vec4  viewport;   // xy=帧缓冲宽高(像素), zw 保留
    layout(offset = 16) vec4  grid;       // x=列数 y=行数（浮点编码，见 C++ 端）
    layout(offset = 32) vec4  color;      // 边界颜色 RGBA
    layout(offset = 48) vec4  clearColor; // 背景清屏色 RGBA
} pc;

layout(location = 0) flat out vec4 vColor;

vec2 toNdc(vec2 pixel) {
    // 像素坐标原点左上、y 向下；映射到 NDC（y 同样向下，即 -y 在屏幕下方）
    return vec2(pixel.x / pc.viewport.x * 2.0 - 1.0,
                pixel.y / pc.viewport.y * 2.0 - 1.0);
}

// 边的两个端点（像素坐标）。edgeId: 0=上 1=右 2=下 3=左
vec2 edgePoint(uint col, uint row, uint edgeId, uint endpoint) {
    // 整体内缩半像素，避免外轮廓落在视口像素边界被光栅化规则剔除
    vec2 margin = vec2(0.5);
    vec2 usable = pc.viewport.xy - vec2(1.0);
    vec2 cellSize = vec2(usable.x / pc.grid.x, usable.y / pc.grid.y);
    vec2 p0 = margin + vec2(float(col) * cellSize.x, float(row) * cellSize.y);
    vec2 p1 = p0 + cellSize;
    vec2 pts[8];
    pts[0] = vec2(p0.x, p0.y); pts[1] = vec2(p1.x, p0.y); // 上
    pts[2] = vec2(p1.x, p0.y); pts[3] = vec2(p1.x, p1.y); // 右
    pts[4] = vec2(p1.x, p1.y); pts[5] = vec2(p0.x, p1.y); // 下
    pts[6] = vec2(p0.x, p1.y); pts[7] = vec2(p0.x, p0.y); // 左
    return pts[edgeId * 2u + endpoint];
}

void main() {
    vColor = pc.color;

    uint localVertex = gl_VertexIndex;               // 0..7
    uint edgeId      = localVertex / 2u;             // 0..3
    uint endpoint    = localVertex % 2u;             // 0..1
    uint cols        = uint(pc.grid.x);
    uint rows        = uint(pc.grid.y);
    uint col         = gl_InstanceIndex % cols;
    uint row         = gl_InstanceIndex / cols;

    // 网格外轮廓，或与“不存在的邻居”相接的边即为边界
    bool isBoundary = (edgeId == 0u && row == 0u)
                   || (edgeId == 1u && col == cols - 1u)
                   || (edgeId == 2u && row == rows - 1u)
                   || (edgeId == 3u && col == 0u);

    // 先计算本端点位置；非边界边让两端点重合为单元左上角，退化为零长度线段
    vec2 position = toNdc(edgePoint(col, row, edgeId, endpoint));
    if (!isBoundary) {
        position = toNdc(edgePoint(col, row, /*edgeId=*/0u, /*endpoint=*/0u));
    }
    gl_Position = vec4(position, 0.0, 1.0);
}
