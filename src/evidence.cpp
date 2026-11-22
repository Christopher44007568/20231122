#include "evidence.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "json.hpp"
#include "sha256.hpp"

namespace grid {

namespace fs = std::filesystem;

namespace {

std::string timestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm);
    std::ostringstream out;
    out << buffer << "." << std::setfill('0') << std::setw(3) << ms.count();
    return out.str();
}

}  // namespace

bool EvidenceStore::open(const std::string& baseDir, const std::string& runId,
                         const std::string& rawConfigPath, std::string* error) {
    runDir_ = (fs::path(baseDir) / runId).string();
    std::error_code ec;
    fs::create_directories(fs::path(runDir_) / "frames", ec);
    if (ec) {
        *error = "创建运行目录失败: " + ec.message();
        return false;
    }

    // 原始配置原样留存（旧/新格式均保留原始字节）
    if (!rawConfigPath.empty() && fs::exists(rawConfigPath)) {
        fs::copy_file(rawConfigPath, fs::path(runDir_) / "config.raw.json",
                      fs::copy_options::overwrite_existing, ec);
        if (ec) event("evidence", "警告: 原始配置留存失败 " + ec.message());
    }

    eventLogPath_ = (fs::path(runDir_) / "events.log").string();
    std::ofstream touch(eventLogPath_, std::ios::app);
    event("run", "证据目录已建立 " + runDir_);
    return true;
}

void EvidenceStore::event(const std::string& phase, const std::string& detail) {
    std::ofstream log(eventLogPath_, std::ios::app);
    if (log) {
        log << "{\"t\":\"" << minijson::escape(timestamp())
            << "\",\"phase\":\"" << minijson::escape(phase)
            << "\",\"detail\":\"" << minijson::escape(detail) << "\"}\n";
    }
}

uint64_t EvidenceStore::countPixelDiff(const uint8_t* current, uint64_t byteLength) const {
    if (previousPixels_.size() != static_cast<size_t>(byteLength)) return 0;
    uint64_t diff = 0;
    for (uint64_t i = 0; i < byteLength; i += 4) {
        if (std::memcmp(previousPixels_.data() + i, current + i, 4) != 0) ++diff;
    }
    return diff;
}

uint64_t EvidenceStore::countBoundaryPixels(const uint8_t* rgba, uint32_t width,
                                            uint32_t height,
                                            const float clearColor[4]) const {
    uint8_t ref[4];
    for (int i = 0; i < 4; ++i) {
        ref[i] = static_cast<uint8_t>(
            clearColor[i] * 255.0f + (clearColor[i] >= 0.0f ? 0.5f : -0.5f));
    }
    uint64_t count = 0;
    uint64_t pixels = static_cast<uint64_t>(width) * height;
    for (uint64_t p = 0; p < pixels; ++p) {
        const uint8_t* px = rgba + p * 4;
        if (std::memcmp(px, ref, 3) != 0) ++count;  // 忽略 alpha，比较 RGB
    }
    return count;
}

bool EvidenceStore::savePpm(uint32_t frameIndex, const uint8_t* rgba, uint32_t width,
                            uint32_t height, const float clearColor[4],
                            FrameEvidence* out, std::string* error) {
    uint64_t byteLength = static_cast<uint64_t>(width) * height * 4;
    out->frameIndex = frameIndex;
    out->pixelSha256 = crypto::Sha256::ofBytes(rgba, static_cast<size_t>(byteLength));
    out->boundaryPixels = countBoundaryPixels(rgba, width, height, clearColor);

    std::ostringstream name;
    name << "frames/frame_" << std::setfill('0') << std::setw(4) << frameIndex << ".ppm";
    std::string relative = name.str();
    std::string full = (fs::path(runDir_) / relative).string();

    std::ofstream ppm(full, std::ios::binary);
    if (!ppm) {
        *error = "无法写入截图 " + full;
        return false;
    }
    ppm << "P6\n" << width << " " << height << "\n255\n";
    std::vector<uint8_t> rgb(static_cast<size_t>(byteLength) / 4 * 3);
    for (size_t p = 0, i = 0; p < byteLength; p += 4, i += 3) {
        rgb[i] = rgba[p];
        rgb[i + 1] = rgba[p + 1];
        rgb[i + 2] = rgba[p + 2];
    }
    ppm.write(reinterpret_cast<const char*>(rgb.data()),
              static_cast<std::streamsize>(rgb.size()));
    ppm.close();

    out->imagePath = relative;
    out->imageSha256 = crypto::Sha256::ofFile(full);
    out->diffPixelsFromPrevious = countPixelDiff(rgba, byteLength);

    previousPixels_.assign(rgba, rgba + byteLength);
    frames_.push_back(*out);
    return true;
}

void EvidenceStore::appendFrame(const FrameEvidence& frame) {
    frames_.push_back(frame);
}

void EvidenceStore::writeManifest(const std::string& extraJson) const {
    std::ostringstream json;
    json << "{\n  \"frames\": [\n";
    for (size_t i = 0; i < frames_.size(); ++i) {
        const auto& f = frames_[i];
        json << "    {"
             << "\"frame\":" << f.frameIndex
             << ",\"draw_calls\":" << f.drawCallCount
             << ",\"image\":\"" << minijson::escape(f.imagePath) << "\""
             << ",\"image_sha256\":\"" << f.imageSha256 << "\""
             << ",\"pixel_sha256\":\"" << f.pixelSha256 << "\""
             << ",\"boundary_pixels\":" << f.boundaryPixels
             << ",\"diff_vs_previous\":" << f.diffPixelsFromPrevious
             << ",\"final_layout\":\"" << minijson::escape(f.finalImageLayout) << "\""
             << "}";
        if (i + 1 < frames_.size()) json << ",";
        json << "\n";
    }
    json << "  ],\n";
    json << extraJson;
    json << "\n}\n";

    std::ofstream out(fs::path(runDir_) / "manifest.json", std::ios::binary);
    out << json.str();
}

}  // namespace grid
