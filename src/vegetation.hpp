#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vegd {

constexpr uint32_t kTilesX = 8;
constexpr uint32_t kTilesY = 8;
constexpr uint32_t kTileCount = kTilesX * kTilesY;
constexpr uint32_t kTileSize = 512;
constexpr uint32_t kMaxInstancesPerTile = 4096;
constexpr uint64_t kDefaultBudgetBytes = 16ull * 1024ull * 1024ull;

struct Params {
    uint64_t seed = 0x20231122D15EA5E5ull;
    uint32_t densityMilli = 8800;
    uint32_t jitterMilli = 4200;
    uint32_t altitudeMilli = 72000;
    uint32_t slopeBiasMilli = 900;
    uint32_t windMilli = 120000;
    uint32_t boundaryBoostMilli = 1750;
    uint32_t maxInstancesPerTile = kMaxInstancesPerTile;
    uint32_t missingStart = 28;
    uint32_t missingEnd = 36;

    static Params extreme();
    uint64_t hash() const;
};

struct Instance {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32_t kind = 0;
};
static_assert(sizeof(Instance) == 16, "Instance must stay GPU-layout compatible");

struct TileData {
    uint32_t tile = 0;
    uint32_t count = 0;
    std::vector<Instance> instances;
    uint64_t hash = 0;
    bool completed = false;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct BoundarySample {
    bool valid = false;
    uint32_t tile = 0;
    uint32_t acceptedCount = 0;
    Vec3 firstPos;
    uint64_t tileHash = 0;
};

struct ResourcePlan {
    uint64_t budgetBytes = kDefaultBudgetBytes;
    uint32_t capacity = kMaxInstancesPerTile;
    std::vector<std::string> degradations;
};

struct PushConstants {
    uint32_t tile;
    uint32_t seedLo;
    uint32_t seedHi;
    uint32_t densityMilli;
    uint32_t jitterMilli;
    uint32_t altitudeMilli;
    uint32_t slopeBiasMilli;
    uint32_t windMilli;
    uint32_t capacity;
    uint32_t tileSize;
    uint32_t boundaryBoostMilli;
    uint32_t reserved;
};
static_assert(sizeof(PushConstants) == 48, "push constant layout changed");

bool isMissingTile(const Params& params, uint32_t tile);
uint32_t boundaryBoostForTile(const Params& params, uint32_t tile);
uint32_t targetCountForTile(const Params& params, uint32_t tile, uint32_t capacity);
ResourcePlan planResources(const Params& params, uint64_t budgetBytes);
PushConstants makePushConstants(const Params& params, uint32_t tile, uint32_t capacity);

uint32_t hash32(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
TileData generateTileCpu(const Params& params, uint32_t tile, uint32_t capacity);
TileData makeTileFromInstances(const Params& params, uint32_t tile, uint32_t count,
                               const std::vector<Instance>& instances);
uint64_t hashBytes(const void* data, size_t size, uint64_t seed = 1469598103934665603ull);
uint64_t hashTile(const TileData& tile);
uint64_t hashAggregate(const std::vector<TileData>& tiles);
BoundarySample sampleBoundary(const Params& params, const std::vector<TileData>& tiles,
                              uint32_t tile);

std::string hex64(uint64_t value);
std::string trimFloat(float value);

}  // namespace vegd
