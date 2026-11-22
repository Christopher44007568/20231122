#include "state.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <type_traits>

namespace vegd {
namespace {

constexpr char kMagic[8] = {'V', 'E', 'G', 'S', 'T', '0', '0', '1'};
constexpr uint64_t kFnvPrime = 1099511628211ull;

void appendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

template <typename T>
void appendValue(std::vector<uint8_t>& out, T value) {
    static_assert(std::is_trivially_copyable<T>::value, "raw state field");
    appendBytes(out, &value, sizeof(value));
}

template <typename T>
bool readValue(const std::vector<uint8_t>& bytes, size_t& offset, T& value) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        return false;
    }
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

uint64_t checksumOf(const std::vector<uint8_t>& bytes) {
    uint64_t checksum = 1469598103934665603ull;
    for (uint8_t byte : bytes) {
        checksum ^= byte;
        checksum *= kFnvPrime;
    }
    return checksum;
}

void appendParams(std::vector<uint8_t>& out, const Params& params) {
    appendValue(out, params.seed);
    appendValue(out, params.densityMilli);
    appendValue(out, params.jitterMilli);
    appendValue(out, params.altitudeMilli);
    appendValue(out, params.slopeBiasMilli);
    appendValue(out, params.windMilli);
    appendValue(out, params.boundaryBoostMilli);
    appendValue(out, params.maxInstancesPerTile);
    appendValue(out, params.missingStart);
    appendValue(out, params.missingEnd);
}

bool readParams(const std::vector<uint8_t>& bytes, size_t& offset, Params& params) {
    return readValue(bytes, offset, params.seed) &&
           readValue(bytes, offset, params.densityMilli) &&
           readValue(bytes, offset, params.jitterMilli) &&
           readValue(bytes, offset, params.altitudeMilli) &&
           readValue(bytes, offset, params.slopeBiasMilli) &&
           readValue(bytes, offset, params.windMilli) &&
           readValue(bytes, offset, params.boundaryBoostMilli) &&
           readValue(bytes, offset, params.maxInstancesPerTile) &&
           readValue(bytes, offset, params.missingStart) &&
           readValue(bytes, offset, params.missingEnd);
}

}  // namespace

Checkpoint makeEmptyCheckpoint(const Params& params, uint32_t capacity,
                               uint64_t budgetBytes) {
    Checkpoint checkpoint;
    checkpoint.params = params;
    checkpoint.effectiveCapacity = capacity;
    checkpoint.budgetBytes = budgetBytes;
    checkpoint.aggregateHash = hashAggregate(checkpointTiles(checkpoint));
    return checkpoint;
}

void applyTile(Checkpoint& checkpoint, TileData tile) {
    if (tile.tile >= kTileCount) {
        return;
    }
    checkpoint.counts[tile.tile] = tile.count;
    checkpoint.tileHashes[tile.tile] = tile.hash;
    checkpoint.instances[tile.tile] = std::move(tile.instances);
    if (tile.completed) {
        checkpoint.completedMask |= (1ull << tile.tile);
    }
}

bool isTileCompleted(const Checkpoint& checkpoint, uint32_t tile) {
    return tile < kTileCount && (checkpoint.completedMask & (1ull << tile)) != 0;
}

std::vector<TileData> checkpointTiles(const Checkpoint& checkpoint) {
    std::vector<TileData> tiles(kTileCount);
    for (uint32_t tile = 0; tile < kTileCount; ++tile) {
        tiles[tile].tile = tile;
        tiles[tile].count = checkpoint.counts[tile];
        tiles[tile].instances = checkpoint.instances[tile];
        tiles[tile].hash = checkpoint.tileHashes[tile];
        tiles[tile].completed = isTileCompleted(checkpoint, tile);
    }
    return tiles;
}

StateIoResult saveCheckpoint(const std::string& path,
                             const Checkpoint& checkpoint) {
    StateIoResult result;
    if (checkpoint.nextTile > kTileCount) {
        result.error = "nextTile out of range";
        return result;
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(2048 + static_cast<size_t>(checkpoint.completedMask) * 64);
    appendBytes(bytes, kMagic, sizeof(kMagic));
    appendValue(bytes, kStateVersion);
    appendValue(bytes, static_cast<uint32_t>(0));
    appendParams(bytes, checkpoint.params);
    appendValue(bytes, checkpoint.nextTile);
    appendValue(bytes, checkpoint.effectiveCapacity);
    appendValue(bytes, checkpoint.budgetBytes);
    appendValue(bytes, checkpoint.aggregateHash);
    appendValue(bytes, checkpoint.completedMask);
    appendValue(bytes, static_cast<uint32_t>(kTileCount));
    appendValue(bytes, static_cast<uint32_t>(0));
    for (uint32_t tile = 0; tile < kTileCount; ++tile) {
        appendValue(bytes, checkpoint.counts[tile]);
        appendValue(bytes, checkpoint.tileHashes[tile]);
    }
    for (uint32_t tile = 0; tile < kTileCount; ++tile) {
        if (!isTileCompleted(checkpoint, tile)) {
            continue;
        }
        const auto& instances = checkpoint.instances[tile];
        appendValue(bytes, static_cast<uint32_t>(instances.size()));
        if (!instances.empty()) {
            appendBytes(bytes, instances.data(),
                        instances.size() * sizeof(Instance));
        }
    }
    const uint64_t checksum = checksumOf(bytes);
    appendValue(bytes, checksum);

    std::error_code ec;
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            result.error = "cannot create state directory: " + ec.message();
            return result;
        }
    }
    const std::filesystem::path temporary = target.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            result.error = "cannot open temporary checkpoint";
            return result;
        }
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream) {
            result.error = "checkpoint write failed";
            return result;
        }
    }
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        result.error = "checkpoint rename failed: " + ec.message();
        return result;
    }
    result.ok = true;
    result.bytes = bytes.size();
    result.checksum = checksum;
    return result;
}

StateLoadResult loadCheckpoint(const std::string& path) {
    StateLoadResult result;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        result.error = "cannot open checkpoint";
        return result;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    result.bytes = bytes.size();
    if (bytes.size() < sizeof(kMagic) + sizeof(uint64_t)) {
        result.error = "checkpoint is truncated";
        return result;
    }

    uint64_t storedChecksum = 0;
    std::memcpy(&storedChecksum,
                bytes.data() + bytes.size() - sizeof(storedChecksum),
                sizeof(storedChecksum));
    bytes.resize(bytes.size() - sizeof(storedChecksum));
    const uint64_t actualChecksum = checksumOf(bytes);
    result.checksum = actualChecksum;
    if (storedChecksum != actualChecksum) {
        result.error = "checkpoint checksum mismatch";
        return result;
    }

    size_t offset = 0;
    char magic[8]{};
    if (!readValue(bytes, offset, magic) ||
        std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        result.error = "checkpoint magic mismatch";
        return result;
    }
    uint32_t version = 0;
    uint32_t reserved = 0;
    if (!readValue(bytes, offset, version) || version != kStateVersion) {
        result.error = "unsupported checkpoint version";
        return result;
    }
    if (!readValue(bytes, offset, reserved)) {
        result.error = "checkpoint header truncated";
        return result;
    }
    Checkpoint checkpoint;
    if (!readParams(bytes, offset, checkpoint.params)) {
        result.error = "checkpoint params truncated";
        return result;
    }
    uint32_t tileCount = 0;
    if (!readValue(bytes, offset, checkpoint.nextTile) ||
        !readValue(bytes, offset, checkpoint.effectiveCapacity) ||
        !readValue(bytes, offset, checkpoint.budgetBytes) ||
        !readValue(bytes, offset, checkpoint.aggregateHash) ||
        !readValue(bytes, offset, checkpoint.completedMask) ||
        !readValue(bytes, offset, tileCount) ||
        !readValue(bytes, offset, reserved)) {
        result.error = "checkpoint header values truncated";
        return result;
    }
    if (tileCount != kTileCount || checkpoint.nextTile > kTileCount ||
        checkpoint.effectiveCapacity == 0 ||
        checkpoint.effectiveCapacity > kMaxInstancesPerTile) {
        result.error = "checkpoint tile layout mismatch";
        return result;
    }
    for (uint32_t tile = 0; tile < kTileCount; ++tile) {
        if (!readValue(bytes, offset, checkpoint.counts[tile]) ||
            !readValue(bytes, offset, checkpoint.tileHashes[tile])) {
            result.error = "checkpoint tile table truncated";
            return result;
        }
    }
    for (uint32_t tile = 0; tile < kTileCount; ++tile) {
        if (!isTileCompleted(checkpoint, tile)) {
            continue;
        }
        uint32_t count = 0;
        if (!readValue(bytes, offset, count) ||
            count > checkpoint.effectiveCapacity) {
            result.error = "checkpoint instance count invalid";
            return result;
        }
        if (offset > bytes.size() ||
            static_cast<uint64_t>(count) * sizeof(Instance) >
                bytes.size() - offset) {
            result.error = "checkpoint instance payload truncated";
            return result;
        }
        checkpoint.instances[tile].resize(count);
        if (count > 0) {
            std::memcpy(checkpoint.instances[tile].data(), bytes.data() + offset,
                        count * sizeof(Instance));
            offset += count * sizeof(Instance);
        }
        if (count != checkpoint.counts[tile]) {
            result.error = "checkpoint count/table mismatch";
            return result;
        }
    }
    if (offset != bytes.size()) {
        result.error = "checkpoint has trailing bytes";
        return result;
    }
    if (hashAggregate(checkpointTiles(checkpoint)) !=
        checkpoint.aggregateHash) {
        result.error = "checkpoint aggregate hash mismatch";
        return result;
    }
    result.ok = true;
    result.checkpoint = std::move(checkpoint);
    return result;
}

}  // namespace vegd
