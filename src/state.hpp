#pragma once

#include "vegetation.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace vegd {

constexpr uint32_t kStateVersion = 1;

struct Checkpoint {
    Params params;
    uint32_t nextTile = 0;
    uint32_t effectiveCapacity = kMaxInstancesPerTile;
    uint64_t budgetBytes = kDefaultBudgetBytes;
    uint64_t aggregateHash = 0;
    uint64_t completedMask = 0;
    std::array<uint32_t, kTileCount> counts{};
    std::array<uint64_t, kTileCount> tileHashes{};
    std::array<std::vector<Instance>, kTileCount> instances;
};

struct StateIoResult {
    bool ok = false;
    uint64_t bytes = 0;
    uint64_t checksum = 0;
    std::string error;
};

struct StateLoadResult {
    bool ok = false;
    uint64_t bytes = 0;
    uint64_t checksum = 0;
    Checkpoint checkpoint;
    std::string error;
};

Checkpoint makeEmptyCheckpoint(const Params& params, uint32_t capacity,
                               uint64_t budgetBytes);
void applyTile(Checkpoint& checkpoint, TileData tile);
std::vector<TileData> checkpointTiles(const Checkpoint& checkpoint);
bool isTileCompleted(const Checkpoint& checkpoint, uint32_t tile);

StateIoResult saveCheckpoint(const std::string& path, const Checkpoint& checkpoint);
StateLoadResult loadCheckpoint(const std::string& path);

}  // namespace vegd
