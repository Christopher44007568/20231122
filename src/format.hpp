#pragma once

#include "common.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace inst {

constexpr uint32_t kMagicV1 = 0x31444D49u; // "IMD1" little-endian
constexpr uint32_t kMagicV2 = 0x32444D49u; // "IMD2" little-endian
constexpr uint32_t kMaxRecordsPerObject = 256;

enum class FormatRevision : uint32_t {
    LegacyV1 = 1,
    CurrentV2 = 2,
    Unknown = 0,
};

// One model submission. The same logical record can be encoded by both
// revisions; legacySelector models the old implicit lookup, while model is
// the explicit v2 selection key.
struct Submission {
    uint32_t objectId = 0;
    uint32_t revision = 1;
    uint32_t legacySelector = 0;
    uint32_t model = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float scale = 1.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float yaw = 0.0f;

    uint32_t batchId = 0;
    uint64_t sourceSequence = 0;
    std::string sourcePath;
    std::string sourceExtension;
    std::string sourceHash;
    FormatRevision formatRevision = FormatRevision::Unknown;
};

struct InputObject {
    std::string path;
    std::string extension;
    std::string contentHash;
    uint32_t batchId = 0;
    FormatRevision revision = FormatRevision::Unknown;
    std::vector<Submission> records;
};

const char* revisionName(FormatRevision revision);
const char* modelName(uint32_t model);

InputObject loadInputObject(const fs::path& path, uint64_t& nextSequence);

// Deterministic fixture writer used by tools/generate_fixtures.cpp. It writes
// the same logical records in either container revision.
void writeInputObject(const fs::path& path, FormatRevision revision,
                      uint32_t batchId,
                      const std::vector<Submission>& records);

} // namespace inst
