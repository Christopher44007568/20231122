#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace grid {

struct FrameEvidence {
    uint32_t frameIndex = 0;             // 0 基
    std::string imagePath;               // PPM 截图相对路径
    std::string imageSha256;             // 截图文件指纹
    std::string pixelSha256;             // 纯像素字节指纹（剔除文件头）
    uint64_t boundaryPixels = 0;         // 与清屏色不同的像素数（边界覆盖）
    uint64_t diffPixelsFromPrevious = 0; // 与上一帧不同像素数（首帧记 0）
    uint32_t drawCallCount = 0;          // 该帧记录的 draw call 数
    std::string finalImageLayout;        // 帧缓冲最终 image layout
};

// 证据中心：所有写入都是追加/落盘型，进程异常退出也能保留已产生的证据
class EvidenceStore {
public:
    // 创建 artifacts/runs/<runId>/ 目录，保存原始配置副本，打开事件流
    bool open(const std::string& baseDir, const std::string& runId,
              const std::string& rawConfigPath, std::string* error);

    const std::string& runDir() const { return runDir_; }

    // 带时间戳的结构化事件，立即 flush
    void event(const std::string& phase, const std::string& detail);

    // 保存 RGBA8 截图为 PPM(P6)，返回相对 runDir 的路径与双指纹
    bool savePpm(uint32_t frameIndex, const uint8_t* rgba, uint32_t width,
                 uint32_t height, const float clearColor[4],
                 FrameEvidence* out, std::string* error);

    // 与上一帧逐像素比较
    uint64_t countPixelDiff(const uint8_t* current, uint64_t byteLength) const;
    uint64_t countBoundaryPixels(const uint8_t* rgba, uint32_t width,
                                 uint32_t height, const float clearColor[4]) const;

    void appendFrame(const FrameEvidence& frame);
    void writeManifest(const std::string& extraJson) const;

private:
    std::string runDir_;
    std::string eventLogPath_;
    std::vector<uint8_t> previousPixels_;
    std::vector<FrameEvidence> frames_;
};

}  // namespace grid
