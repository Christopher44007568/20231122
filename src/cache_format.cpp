#include "cache_format.hpp"

#include "hash.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace lshc {
namespace {

constexpr uint16_t kLegacyVersion = 1;
constexpr uint16_t kCurrentVersion = 2;
constexpr uint32_t kLegacyStride = 48;
constexpr uint32_t kCurrentStride = 64;

uint16_t readU16(const std::vector<uint8_t>& data, std::size_t offset)
{
    if (offset + 2u > data.size()) {
        throw std::runtime_error("cache header is truncated");
    }
    return static_cast<uint16_t>(data[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1u]) << 8u);
}

uint32_t readU32(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8u) |
           (static_cast<uint32_t>(data[2]) << 16u) |
           (static_cast<uint32_t>(data[3]) << 24u);
}

uint64_t readU64(const uint8_t* data)
{
    return static_cast<uint64_t>(readU32(data)) |
           (static_cast<uint64_t>(readU32(data + 4)) << 32u);
}

float readFloat(const uint8_t* data)
{
    const uint32_t bits = readU32(data);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(bits), "unexpected float width");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool isZeroTail(const std::vector<uint8_t>& data, std::size_t offset)
{
    return std::all_of(data.begin() + static_cast<std::ptrdiff_t>(offset),
                       data.end(), [](uint8_t value) { return value == 0u; });
}

ShadowRecord decodeCompleteRecord(const uint8_t* raw, uint32_t stride,
                                  uint64_t offset, bool legacy)
{
    ShadowRecord record;
    record.legacy = legacy;
    record.objectId = readU32(raw);
    record.drawCall = readU32(raw + 4);
    record.centerX = readFloat(raw + 8);
    record.centerY = readFloat(raw + 12);
    record.radius = readFloat(raw + 16);
    record.lightX = readFloat(raw + 20);
    record.lightY = readFloat(raw + 24);
    record.lightZ = readFloat(raw + 28);
    record.strength = readFloat(raw + 32);
    record.penumbra = readFloat(raw + 36);
    record.resourceRevision = readU32(raw + 40);
    record.storedCrc = readU32(raw + 44);
    record.rawOffset = offset;
    record.rawSize = stride;
    if (!legacy) {
        record.pipelineId = readU32(raw + 48);
        record.contentHash = readU64(raw + 56);
    }
    return record;
}

void appendIntegrityAnomalies(ParseResult& result, const uint8_t* raw,
                              const ShadowRecord& record)
{
    const uint32_t actualCrc = crc32c(raw, 44);
    if (actualCrc != record.storedCrc) {
        RecordAnomaly anomaly;
        anomaly.kind = "record_crc_mismatch";
        anomaly.detail = "stored CRC-32C does not cover the fixed record prefix";
        anomaly.objectId = record.objectId;
        anomaly.rawOffset = record.rawOffset;
        anomaly.raw.assign(raw, raw + record.rawSize);
        result.anomalies.push_back(std::move(anomaly));
        return;
    }
    if (!record.legacy) {
        const uint64_t actualHash = fnv1a64(raw, 56);
        if (actualHash != record.contentHash || record.pipelineId == 0u) {
            RecordAnomaly anomaly;
            anomaly.kind = "record_content_hash_mismatch";
            anomaly.detail = "current-format content hash or pipeline id is invalid";
            anomaly.objectId = record.objectId;
            anomaly.rawOffset = record.rawOffset;
            anomaly.raw.assign(raw, raw + record.rawSize);
            result.anomalies.push_back(std::move(anomaly));
        }
    }
}

bool recordMatches(uint32_t objectId, const ShadowRecord& record)
{
    return record.objectId == objectId;
}

void markIntegrity(ParseResult& result)
{
    for (const RecordAnomaly& anomaly : result.anomalies) {
        if (anomaly.kind.rfind("record_", 0) != 0) {
            continue;
        }
        for (ShadowRecord& record : result.records) {
            if (recordMatches(anomaly.objectId, record) &&
                record.rawOffset == anomaly.rawOffset) {
                record.integrityOk = false;
            }
        }
    }
}

}  // namespace

ParseResult parseShadowCache(const std::string& path, const std::string& label)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open snapshot: " + path);
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() < 16u) {
        throw std::runtime_error("snapshot header is truncated: " + path);
    }
    if (std::memcmp(bytes.data(), "LSHC", 4) != 0) {
        throw std::runtime_error("bad shadow-cache magic: " + path);
    }

    ParseResult result;
    SnapshotInfo& snapshot = result.snapshot;
    snapshot.label = label;
    snapshot.path = path;
    snapshot.formatVersion = readU16(bytes, 4);
    const uint16_t headerSize = readU16(bytes, 6);

    if (snapshot.formatVersion == kLegacyVersion) {
        if (headerSize != 16u) {
            throw std::runtime_error("legacy snapshot has unsupported header size");
        }
        snapshot.formatName = "legacy-v1";
        snapshot.declaredRecordCount = readU32(bytes.data() + 8);
        snapshot.recordStride = readU32(bytes.data() + 12);
        if (snapshot.recordStride != kLegacyStride) {
            throw std::runtime_error("legacy snapshot has unsupported record stride");
        }
        snapshot.snapshotId = fnv1a64(bytes.data(), bytes.size());
    } else if (snapshot.formatVersion == kCurrentVersion) {
        if (headerSize != 24u || bytes.size() < 24u) {
            throw std::runtime_error("current snapshot has unsupported header size");
        }
        snapshot.formatName = "current-v2";
        snapshot.declaredRecordCount = readU32(bytes.data() + 8);
        snapshot.recordStride = readU32(bytes.data() + 12);
        snapshot.snapshotId = readU64(bytes.data() + 16);
        if (snapshot.recordStride != kCurrentStride) {
            throw std::runtime_error("current snapshot has unsupported record stride");
        }
    } else {
        throw std::runtime_error("unsupported shadow-cache version " +
                                 std::to_string(snapshot.formatVersion));
    }

    snapshot.fileSize = bytes.size();
    snapshot.recordDataOffset = headerSize;
    snapshot.headerBytes.assign(bytes.begin(), bytes.begin() + headerSize);
    snapshot.sha256 = sha256Hex(bytes);

    const uint64_t declaredBytes =
        static_cast<uint64_t>(snapshot.declaredRecordCount) *
        static_cast<uint64_t>(snapshot.recordStride);
    if (declaredBytes >
        static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("declared snapshot payload is too large");
    }

    for (uint32_t index = 0; index < snapshot.declaredRecordCount; ++index) {
        const uint64_t offset = snapshot.recordDataOffset +
                                static_cast<uint64_t>(index) *
                                    snapshot.recordStride;
        if (offset >= snapshot.fileSize) {
            TruncatedRecord truncated;
            truncated.declaredIndex = index;
            truncated.offset = offset;
            truncated.expectedBytes = snapshot.recordStride;
            result.truncated = std::move(truncated);
            break;
        }
        const uint64_t remaining = snapshot.fileSize - offset;
        if (remaining < snapshot.recordStride) {
            TruncatedRecord truncated;
            truncated.declaredIndex = index;
            truncated.offset = offset;
            truncated.availableBytes = remaining;
            truncated.expectedBytes = snapshot.recordStride;
            truncated.rawTail.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
            if (remaining >= 4u) {
                truncated.hasObjectId = true;
                truncated.partialObjectId = readU32(bytes.data() + offset);
            }
            if (remaining >= 8u) {
                truncated.hasDrawCall = true;
                truncated.partialDrawCall = readU32(bytes.data() + offset + 4u);
            }
            result.truncated = std::move(truncated);
            break;
        }

        const auto* raw = bytes.data() + static_cast<std::ptrdiff_t>(offset);
        const bool legacy = snapshot.formatVersion == kLegacyVersion;
        result.records.push_back(
            decodeCompleteRecord(raw, snapshot.recordStride, offset, legacy));
        appendIntegrityAnomalies(result, raw, result.records.back());
    }

    const uint64_t consumed = snapshot.recordDataOffset +
                              static_cast<uint64_t>(result.records.size()) *
                                  snapshot.recordStride;
    if (!result.truncated && consumed < snapshot.fileSize &&
        !isZeroTail(bytes, static_cast<std::size_t>(consumed))) {
        RecordAnomaly anomaly;
        anomaly.kind = "trailing_bytes";
        anomaly.detail = "bytes follow the declared record table";
        anomaly.rawOffset = consumed;
        anomaly.raw.assign(bytes.begin() + static_cast<std::ptrdiff_t>(consumed),
                           bytes.end());
        result.anomalies.push_back(std::move(anomaly));
    }

    markIntegrity(result);
    return result;
}

}  // namespace lshc
