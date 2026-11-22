#include "config.hpp"

#include <fstream>
#include <sstream>

#include "json.hpp"

namespace grid {

namespace {

std::string readFile(const std::string& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        *error = "无法打开配置文件: " + path;
        return "";
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void readColor(const minijson::Value& value, float out[4]) {
    if (value.type != minijson::Value::Type::Array || value.array.size() < 3) return;
    for (size_t i = 0; i < value.array.size() && i < 4; ++i) {
        out[i] = static_cast<float>(value.array[i].asNumber(out[i]));
    }
    if (value.array.size() == 3) out[3] = 1.0f;
}

uint32_t clampu32(double value, uint32_t minValue, uint32_t maxValue) {
    if (value < static_cast<double>(minValue)) return minValue;
    if (value > static_cast<double>(maxValue)) return maxValue;
    return static_cast<uint32_t>(value);
}

}  // namespace

Config loadConfig(const std::string& path, std::string* error) {
    Config config;
    config.rawPath = path;
    std::string text = readFile(path, error);
    if (!error->empty()) return config;

    minijson::Value root = minijson::parse(text, error);
    if (!error->empty()) return config;
    if (!root.isObject()) {
        *error = "配置根节点必须是 JSON 对象";
        return config;
    }

    // 无 format 字段时按旧格式处理；format=1 旧，format=2 新
    int format = 1;
    if (root.has("format")) {
        format = static_cast<int>(root.get("format").asNumber(1));
    } else if (root.has("version") &&
               root.get("version").type == minijson::Value::Type::String &&
               root.get("version").asString().rfind("v2", 0) == 0) {
        format = 2;
    }
    config.formatVersion = format;

    if (format >= 2) {
        // 新格式：嵌套结构
        const auto& render = root.get("render");
        const auto& grid = root.get("grid");
        const auto& run = root.get("run");
        const auto& cancel = root.get("cancel");

        config.width = clampu32(render.get("width").asNumber(config.width), 16, 8192);
        config.height = clampu32(render.get("height").asNumber(config.height), 16, 8192);
        config.headless = render.has("headless") ? render.get("headless").asBool(true) : true;
        config.validation = render.has("validation")
                                ? render.get("validation").asBool(true) : true;
        if (render.has("clear_color")) readColor(render.get("clear_color"), config.clearColor);
        if (render.has("line_color")) readColor(render.get("line_color"), config.lineColor);

        config.gridCols = clampu32(grid.get("columns").asNumber(config.gridCols), 1, 4096);
        config.gridRows = clampu32(grid.get("rows").asNumber(config.gridRows), 1, 4096);

        config.totalFrames = clampu32(run.get("frames").asNumber(config.totalFrames), 1, 100000);
        if (run.has("output_dir")) config.outputDir = run.get("output_dir").asString(config.outputDir);

        if (cancel.type == minijson::Value::Type::Object &&
            cancel.get("enabled").asBool(false)) {
            config.cancelAfterFrame =
                clampu32(cancel.get("after_frame").asNumber(0), 0, config.totalFrames);
        }
    } else {
        // 旧格式：扁平字段（grid 兼容数字或 [cols,rows]）
        config.width = clampu32(root.get("width").asNumber(config.width), 16, 8192);
        config.height = clampu32(root.get("height").asNumber(config.height), 16, 8192);
        if (root.has("line_color")) readColor(root.get("line_color"), config.lineColor);
        if (root.has("clear_color")) readColor(root.get("clear_color"), config.clearColor);
        if (root.has("headless")) config.headless = root.get("headless").asBool(true);
        if (root.has("validation")) config.validation = root.get("validation").asBool(true);
        if (root.has("output_dir")) config.outputDir = root.get("output_dir").asString(config.outputDir);

        const auto& grid = root.get("grid");
        if (grid.type == minijson::Value::Type::Array && grid.array.size() >= 2) {
            config.gridCols = clampu32(grid.array[0].asNumber(config.gridCols), 1, 4096);
            config.gridRows = clampu32(grid.array[1].asNumber(config.gridRows), 1, 4096);
        } else if (grid.type == minijson::Value::Type::Number) {
            uint32_t side = clampu32(grid.asNumber(8), 1, 4096);
            config.gridCols = config.gridRows = side;
        }
        config.totalFrames = clampu32(root.get("frames").asNumber(config.totalFrames), 1, 100000);
        // 旧格式没有取消语义，保留 0（完整跑完）
    }

    if (config.cancelAfterFrame > config.totalFrames) config.cancelAfterFrame = 0;
    return config;
}

std::string describeConfig(const Config& config) {
    std::ostringstream out;
    out << "format_version=" << config.formatVersion
        << " viewport=" << config.width << "x" << config.height
        << " grid=" << config.gridCols << "x" << config.gridRows
        << " frames=" << config.totalFrames
        << " cancel_after_frame=" << config.cancelAfterFrame
        << " headless=" << (config.headless ? "true" : "false")
        << " validation=" << (config.validation ? "true" : "false")
        << " line_color=[" << config.lineColor[0] << "," << config.lineColor[1] << ","
        << config.lineColor[2] << "," << config.lineColor[3] << "]"
        << " clear_color=[" << config.clearColor[0] << "," << config.clearColor[1] << ","
        << config.clearColor[2] << "," << config.clearColor[3] << "]";
    return out.str();
}

}  // namespace grid
