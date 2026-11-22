#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hdrprov {

enum class RecordStatus { Pass, Undecidable, Failed };
enum class RecordKind { Snapshot, Truncated };

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
    bool formatOk = false;
    bool resolutionOk = false;
    std::string magic;
    std::string format;
    std::string resolution;
    std::string software;
    std::string exposure;
    int width = 0;
    int height = 0;
    std::uint64_t headerBytes = 0;
    std::uint64_t pixelBytes = 0;
};

struct Preview {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
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
    std::string revision;
    std::string capturedAt;
    std::string sourcePath;
    std::string sourceRevision;
    std::uint64_t sourceOffset = 0;
    RecordKind kind = RecordKind::Snapshot;
    RecordStatus status = RecordStatus::Failed;
    std::uint64_t fileSize = 0;
    std::uint64_t declaredSize = 0;
    std::string sha256;
    std::vector<std::uint8_t> bytes;
    HeaderInfo header;
    Preview preview;
    std::vector<Step> steps;
    std::vector<Evidence> evidence;
    Conclusion conclusion;
    int changedPixels = 0;
};

struct BatchResult {
    std::string runId;
    std::string startedAt;
    std::string finishedAt;
    std::string tempPath;
    std::vector<Record> records;
    std::vector<std::string> trace;
    bool continuedAfterException = false;
    bool exceptionCaught = false;
    bool tempCleaned = false;
    int passCount = 0;
    int undecidableCount = 0;
    int failedCount = 0;
    int evidenceCount = 0;
};

struct BatchOptions {
    std::filesystem::path root;
    bool writeArtifacts = true;
};

BatchResult processBatch(const BatchOptions& options);
std::string statusLabel(RecordStatus status);
std::string relativeSource(const Record& record);

}  // namespace hdrprov
