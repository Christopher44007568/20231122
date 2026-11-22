#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lshc {

struct ShadowRecord {
    uint32_t objectId = 0;
    uint32_t drawCall = 0;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float radius = 0.0f;
    float lightX = 0.0f;
    float lightY = 0.0f;
    float lightZ = 0.0f;
    float strength = 0.0f;
    float penumbra = 0.0f;
    uint32_t resourceRevision = 0;
    uint32_t pipelineId = 0;
    uint64_t contentHash = 0;
    uint64_t rawOffset = 0;
    uint32_t rawSize = 0;
    uint32_t storedCrc = 0;
    bool legacy = false;
    bool integrityOk = true;
};

struct RecordAnomaly {
    std::string kind;
    std::string detail;
    uint32_t objectId = 0;
    uint64_t rawOffset = 0;
    std::vector<uint8_t> raw;
};

struct TruncatedRecord {
    uint32_t declaredIndex = 0;
    uint64_t offset = 0;
    uint64_t availableBytes = 0;
    uint64_t expectedBytes = 0;
    bool hasObjectId = false;
    uint32_t partialObjectId = 0;
    bool hasDrawCall = false;
    uint32_t partialDrawCall = 0;
    std::vector<uint8_t> rawTail;
};

struct SnapshotInfo {
    std::string label;
    std::string path;
    uint16_t formatVersion = 0;
    std::string formatName;
    uint32_t declaredRecordCount = 0;
    uint32_t recordStride = 0;
    uint64_t snapshotId = 0;
    uint64_t fileSize = 0;
    uint64_t recordDataOffset = 0;
    std::string sha256;
    std::vector<uint8_t> headerBytes;
};

struct ParseResult {
    SnapshotInfo snapshot;
    std::vector<ShadowRecord> records;
    std::vector<RecordAnomaly> anomalies;
    std::optional<TruncatedRecord> truncated;
};

ParseResult parseShadowCache(const std::string& path, const std::string& label);

struct DemoFiles {
    std::filesystem::path legacyPath;
    std::filesystem::path currentPath;
};

DemoFiles writeDemoSnapshots(const std::filesystem::path& directory);

}  // namespace lshc
