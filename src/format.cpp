#include "format.hpp"

#include <cmath>
#include <fstream>

namespace inst {
namespace {

#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    uint16_t version;
    uint16_t headerBytes;
    uint32_t batchId;
    uint32_t recordCount;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t reserved1;
};

struct RecordV1 {
    uint32_t objectId;
    uint32_t revision;
    uint32_t legacySelector;
    uint32_t reserved;
    float x, y, z, scale;
    float r, g, b, yaw;
};

struct RecordV2 {
    uint32_t objectId;
    uint32_t revision;
    uint32_t legacySelector;
    uint32_t model;
    uint32_t flags;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
    float x, y, z, scale;
    float r, g, b, yaw;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 32, "IMD header layout changed");
static_assert(sizeof(RecordV1) == 48, "IMD1 record layout changed");
static_assert(sizeof(RecordV2) == 64, "IMD2 record layout changed");

template <typename T>
T readPod(const std::vector<uint8_t>& bytes, size_t offset,
          const std::string& path) {
    if (offset + sizeof(T) > bytes.size()) {
        throw InputFormatError(path + ": truncated record at byte " +
                               std::to_string(offset));
    }
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

void validateRecord(const Submission& record, const std::string& path) {
    if (record.revision == 0 || record.revision > 2) {
        throw InputFormatError(path + ": invalid record revision " +
                               std::to_string(record.revision));
    }
    if (record.model >= 3 || record.legacySelector >= 3) {
        throw InputFormatError(path + ": model selector outside catalog");
    }
    const float values[] = {record.x, record.y, record.z, record.scale,
                            record.r, record.g, record.b, record.yaw};
    for (float value : values) {
        if (!std::isfinite(value)) {
            throw InputFormatError(path + ": non-finite record field");
        }
    }
    if (record.scale <= 0.0f) {
        throw InputFormatError(path + ": record scale must be positive");
    }
}

void adoptRecord(Submission& out, const RecordV1& raw) {
    out.objectId = raw.objectId;
    out.revision = raw.revision;
    out.legacySelector = raw.legacySelector;
    out.model = raw.legacySelector;
    out.x = raw.x;
    out.y = raw.y;
    out.z = raw.z;
    out.scale = raw.scale;
    out.r = raw.r;
    out.g = raw.g;
    out.b = raw.b;
    out.yaw = raw.yaw;
}

} // namespace

const char* revisionName(FormatRevision revision) {
    switch (revision) {
        case FormatRevision::LegacyV1: return "legacy-v1";
        case FormatRevision::CurrentV2: return "current-v2";
        default: return "unknown";
    }
}

const char* modelName(uint32_t model) {
    switch (model % 3u) {
        case 0: return "cube";
        case 1: return "pyramid";
        default: return "plane";
    }
}

InputObject loadInputObject(const fs::path& path, uint64_t& nextSequence) {
    const std::vector<uint8_t> bytes = readFileBytes(path);
    InputObject object;
    object.path = path.string();
    object.extension = path.extension().string();
    object.contentHash = hex64(fnv1a64(bytes.data(), bytes.size()));

    const Header header = readPod<Header>(bytes, 0, object.path);
    if (header.headerBytes != sizeof(Header)) {
        throw InputFormatError(object.path + ": unsupported header size " +
                               std::to_string(header.headerBytes));
    }
    if (header.recordCount > kMaxRecordsPerObject) {
        throw InputFormatError(object.path + ": record count exceeds limit");
    }
    object.batchId = header.batchId;

    size_t offset = sizeof(Header);
    if (header.magic == kMagicV1) {
        if (header.version != 1) {
            throw InputFormatError(object.path + ": IMD1 version mismatch");
        }
        object.revision = FormatRevision::LegacyV1;
        object.records.reserve(header.recordCount);
        for (uint32_t i = 0; i < header.recordCount; ++i) {
            const RecordV1 raw = readPod<RecordV1>(bytes, offset, object.path);
            offset += sizeof(raw);
            Submission record;
            adoptRecord(record, raw);
            validateRecord(record, object.path);
            record.batchId = object.batchId;
            record.sourceSequence = nextSequence++;
            record.sourcePath = object.path;
            record.sourceExtension = object.extension;
            record.sourceHash = object.contentHash;
            record.formatRevision = object.revision;
            object.records.push_back(record);
        }
    } else if (header.magic == kMagicV2) {
        if (header.version != 2) {
            throw InputFormatError(object.path + ": IMD2 version mismatch");
        }
        object.revision = FormatRevision::CurrentV2;
        object.records.reserve(header.recordCount);
        for (uint32_t i = 0; i < header.recordCount; ++i) {
            const RecordV2 raw = readPod<RecordV2>(bytes, offset, object.path);
            offset += sizeof(raw);
            Submission record;
            record.objectId = raw.objectId;
            record.revision = raw.revision;
            record.legacySelector = raw.legacySelector;
            record.model = raw.model;
            record.x = raw.x;
            record.y = raw.y;
            record.z = raw.z;
            record.scale = raw.scale;
            record.r = raw.r;
            record.g = raw.g;
            record.b = raw.b;
            record.yaw = raw.yaw;
            validateRecord(record, object.path);
            record.batchId = object.batchId;
            record.sourceSequence = nextSequence++;
            record.sourcePath = object.path;
            record.sourceExtension = object.extension;
            record.sourceHash = object.contentHash;
            record.formatRevision = object.revision;
            object.records.push_back(record);
        }
    } else {
        std::ostringstream message;
        message << object.path << ": unknown format id 0x" << std::hex
                << std::setw(8) << std::setfill('0') << header.magic;
        throw InputFormatError(message.str());
    }
    return object;
}

void writeInputObject(const fs::path& path, FormatRevision revision,
                      uint32_t batchId,
                      const std::vector<Submission>& records) {
    if (records.size() > kMaxRecordsPerObject) {
        throw InputFormatError("too many fixture records");
    }
    if (revision != FormatRevision::LegacyV1 &&
        revision != FormatRevision::CurrentV2) {
        throw InputFormatError("fixture revision must be v1 or v2");
    }
    Header header{};
    header.magic = revision == FormatRevision::LegacyV1 ? kMagicV1 : kMagicV2;
    header.version = revision == FormatRevision::LegacyV1 ? 1 : 2;
    header.headerBytes = sizeof(Header);
    header.batchId = batchId;
    header.recordCount = static_cast<uint32_t>(records.size());

    std::ofstream out(path, std::ios::binary);
    if (!out) throw InputFormatError("cannot create " + path.string());
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    for (const Submission& record : records) {
        if (revision == FormatRevision::LegacyV1) {
            RecordV1 raw{};
            raw.objectId = record.objectId;
            raw.revision = record.revision;
            raw.legacySelector = record.legacySelector;
            raw.x = record.x;
            raw.y = record.y;
            raw.z = record.z;
            raw.scale = record.scale;
            raw.r = record.r;
            raw.g = record.g;
            raw.b = record.b;
            raw.yaw = record.yaw;
            out.write(reinterpret_cast<const char*>(&raw), sizeof(raw));
        } else {
            RecordV2 raw{};
            raw.objectId = record.objectId;
            raw.revision = record.revision;
            raw.legacySelector = record.legacySelector;
            raw.model = record.model;
            raw.x = record.x;
            raw.y = record.y;
            raw.z = record.z;
            raw.scale = record.scale;
            raw.r = record.r;
            raw.g = record.g;
            raw.b = record.b;
            raw.yaw = record.yaw;
            out.write(reinterpret_cast<const char*>(&raw), sizeof(raw));
        }
    }
}

} // namespace inst
