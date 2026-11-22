#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace grid {

struct Config {
    int formatVersion = 0;          // 1=旧格式 2=新格式
    uint32_t width = 256;
    uint32_t height = 256;
    uint32_t gridCols = 8;
    uint32_t gridRows = 8;
    uint32_t totalFrames = 5;
    uint32_t cancelAfterFrame = 0;  // 0=不取消；>0 在该帧完成后取消（1 基）
    float lineColor[4] = {0.15f, 0.85f, 0.35f, 1.0f};
    float clearColor[4] = {0.02f, 0.02f, 0.05f, 1.0f};
    bool headless = true;
    bool validation = true;
    std::string outputDir = "artifacts/runs";
    std::string rawPath;            // 原始配置文件路径（证据留存）
};

// 读取并归一化配置；解析失败时 error 非空。
// 旧格式(v1)：扁平字段 width/height/grid/frames/line_color
// 新格式(v2)：嵌套 render.* / grid.* / run.* / cancel.*
Config loadConfig(const std::string& path, std::string* error);

// 归一化后的只读视图，写入 evidence 供复核
std::string describeConfig(const Config& config);

}  // namespace grid
