#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace surfprov {

enum class RecordStatus { Pass, Undecidable, Failed };
enum class RecordKind { Snapshot, UnknownExtension };

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct MeshVertex {
    Vec3 position;
    Vec2 uv;
};

struct Triangle {
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    std::uint16_t c = 0;
};

struct SeamEdge {
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    std::uint16_t orientation = 0;
    std::uint16_t confidence = 0;
    float areaErrorForward = 0.0f;
    float areaErrorReverse = 0.0f;
};

struct Evidence {
    std::string id;
    std::string label;
    std::string inputPath;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    std::string sha256;
    std::string note;
    std::vector<std::uint8_t> excerpt;
};

struct Step {
    std::string id;
    std::string title;
    std::string detail;
    std::string state;
    std::vector<std::string> evidenceIds;
};

struct HeaderInfo {
    bool magicOk = false;
    bool layoutOk = false;
    std::string magic;
    int version = 0;
    std::uint16_t vertexCount = 0;
    std::uint16_t triangleCount = 0;
    std::uint16_t seamCount = 0;
    float uvScale = 1.0f;
    std::uint64_t vertexBytes = 0;
    std::uint64_t indexOffset = 0;
    std::uint64_t indexBytes = 0;
    std::uint64_t seamOffset = 0;
    std::uint64_t seamBytes = 0;
};

struct Preview {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct BoundaryInfo {
    bool present = false;
    std::string id;
    std::string headline;
    std::string detail;
    std::uint16_t seamIndex = 0;
    std::uint16_t orientationField = 0;
    float tolerance = 0.0f;
    float candidateForward = 0.0f;
    float candidateReverse = 0.0f;
    std::vector<std::string> stepIds;
    std::vector<std::string> evidenceIds;
};

struct Conclusion {
    std::string headline;
    std::string detail;
    std::vector<std::string> stepIds;
    std::vector<std::string> evidenceIds;
};

struct Record {
    std::string id;
    std::string name;
    std::string extension;
    std::string revision;
    std::string capturedAt;
    std::string sourcePath;
    std::string sourceRevision;
    std::uint64_t sourceOffset = 0;
    std::uint64_t declaredSize = 0;
    std::uint64_t fileSize = 0;
    std::string sha256;
    RecordKind kind = RecordKind::Snapshot;
    RecordStatus status = RecordStatus::Failed;
    std::vector<std::uint8_t> bytes;
    HeaderInfo header;
    std::vector<MeshVertex> vertices;
    std::vector<Triangle> triangles;
    std::vector<SeamEdge> seams;
    Preview preview;
    std::vector<Step> steps;
    std::vector<Evidence> evidence;
    BoundaryInfo boundary;
    Conclusion conclusion;
    int changedPixels = 0;
};

struct BatchResult {
    std::string runId;
    std::string startedAt;
    std::string finishedAt;
    std::string tempPath;
    std::string inputManifestSha256;
    std::vector<Record> records;
    std::vector<std::string> trace;
    bool continuedAfterException = false;
    bool exceptionCaught = false;
    bool tempCleaned = false;
    int snapshotCount = 0;
    int validSnapshotCount = 0;
    int passCount = 0;
    int undecidableCount = 0;
    int failedCount = 0;
    int boundaryCount = 0;
    int evidenceCount = 0;
};

struct BatchOptions {
    std::filesystem::path root;
    bool writeArtifacts = true;
};

BatchResult processBatch(const BatchOptions& options);
std::string statusLabel(RecordStatus status);
std::string relativeSource(const Record& record);

}  // namespace surfprov
