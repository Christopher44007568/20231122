#include "vegetation.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace vegd {
namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void hashAppend(uint64_t& hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
}

uint32_t rotateLeft(uint32_t value, uint32_t count) {
    return (value << count) | (value >> (32u - count));
}

}  // namespace

Params Params::extreme() {
    Params params;
    params.seed = 0x20231122D15EA5E5ull;
    params.densityMilli = 8800;
    params.jitterMilli = 4200;
    params.altitudeMilli = 72000;
    params.slopeBiasMilli = 900;
    params.windMilli = 120000;
    params.boundaryBoostMilli = 1750;
    params.maxInstancesPerTile = kMaxInstancesPerTile;
    params.missingStart = 28;
    params.missingEnd = 36;
    return params;
}

uint64_t Params::hash() const {
    uint64_t h = kFnvOffset;
    hashAppend(h, &seed, sizeof(seed));
    hashAppend(h, &densityMilli, sizeof(densityMilli));
    hashAppend(h, &jitterMilli, sizeof(jitterMilli));
    hashAppend(h, &altitudeMilli, sizeof(altitudeMilli));
    hashAppend(h, &slopeBiasMilli, sizeof(slopeBiasMilli));
    hashAppend(h, &windMilli, sizeof(windMilli));
    hashAppend(h, &boundaryBoostMilli, sizeof(boundaryBoostMilli));
    hashAppend(h, &maxInstancesPerTile, sizeof(maxInstancesPerTile));
    hashAppend(h, &missingStart, sizeof(missingStart));
    hashAppend(h, &missingEnd, sizeof(missingEnd));
    return h;
}

bool isMissingTile(const Params& params, uint32_t tile) {
    return tile >= params.missingStart && tile < params.missingEnd;
}

uint32_t boundaryBoostForTile(const Params& params, uint32_t tile) {
    if (params.missingStart > 0 && tile == params.missingStart - 1) {
        return params.boundaryBoostMilli;
    }
    if (tile == params.missingEnd) {
        return params.boundaryBoostMilli;
    }
    return 0;
}

uint32_t hash32(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t h = a * 0x9E3779B1u;
    h ^= b * 0x85EBCA77u;
    h = rotateLeft(h, 13);
    h ^= c * 0xC2B2AE3Du;
    h *= 0x27D4EB2Fu;
    h ^= d + 0x165667B1u;
    h ^= h >> 15;
    h *= 0x85EBCA77u;
    h ^= h >> 13;
    return h;
}

uint32_t scaled12(uint32_t value, uint32_t span) {
    return ((value >> 20u) * span) >> 12u;
}

uint32_t targetCountForTile(const Params& params, uint32_t tile, uint32_t capacity) {
    const uint32_t seedLo = static_cast<uint32_t>(params.seed);
    const uint32_t seedHi = static_cast<uint32_t>(params.seed >> 32u);
    const uint32_t base = 240u + (hash32(tile, seedLo, seedHi, 0x11u) % 720u);
    const uint32_t extra = (params.densityMilli * 232u) / 1000u;
    const uint32_t boosted =
        extra + (boundaryBoostForTile(params, tile) * 180u) / 1000u;
    return std::min(capacity, base + boosted);
}

ResourcePlan planResources(const Params& params, uint64_t budgetBytes) {
    ResourcePlan plan;
    plan.budgetBytes = budgetBytes;
    constexpr uint64_t reserve = 64ull * 1024ull;
    uint64_t usable = budgetBytes > reserve ? budgetBytes - reserve : 0;
    uint64_t rawCapacity = usable / (static_cast<uint64_t>(kTilesX) * kTilesY *
                                     sizeof(Instance));
    if (rawCapacity == 0) {
        rawCapacity = 64;
        plan.degradations.push_back("budget-exhausted:minimum-capacity-64");
    }
    plan.capacity = static_cast<uint32_t>(
        std::min<uint64_t>(rawCapacity, params.maxInstancesPerTile));
    if (plan.capacity < params.maxInstancesPerTile) {
        plan.degradations.push_back("instance-capacity-clamped:" +
                                    std::to_string(plan.capacity));
    }
    return plan;
}

PushConstants makePushConstants(const Params& params, uint32_t tile,
                                uint32_t capacity) {
    PushConstants push{};
    push.tile = tile;
    push.seedLo = static_cast<uint32_t>(params.seed);
    push.seedHi = static_cast<uint32_t>(params.seed >> 32u);
    push.densityMilli = params.densityMilli;
    push.jitterMilli = params.jitterMilli;
    push.altitudeMilli = params.altitudeMilli;
    push.slopeBiasMilli = params.slopeBiasMilli;
    push.windMilli = params.windMilli;
    push.capacity = capacity;
    push.tileSize = kTileSize;
    push.boundaryBoostMilli = boundaryBoostForTile(params, tile);
    return push;
}

uint64_t hashBytes(const void* data, size_t size, uint64_t seed) {
    uint64_t h = seed;
    hashAppend(h, data, size);
    return h;
}

TileData generateTileCpu(const Params& params, uint32_t tile, uint32_t capacity) {
    TileData result;
    result.tile = tile;
    if (isMissingTile(params, tile)) {
        result.hash = hashTile(result);
        return result;
    }
    const PushConstants push = makePushConstants(params, tile, capacity);
    const uint32_t count = targetCountForTile(params, tile, capacity);
    result.instances.reserve(count);
    const uint32_t tileColumn = tile & 7u;
    const uint32_t tileRow = tile >> 3u;
    for (uint32_t lane = 0; lane < count; ++lane) {
        const uint32_t hx = hash32(tile, lane, push.seedLo, 0xA511E9B3u);
        const uint32_t hy = hash32(tile, lane, push.seedHi, 0x63D83595u);
        const uint32_t hz =
            hash32(tile, lane, push.seedLo ^ push.seedHi, 0x7FEB352Du);
        const uint32_t hk = hash32(tile, lane, push.tileSize, 0x846CA68Bu);

        const int32_t jitter =
            static_cast<int32_t>(scaled12(hy, push.jitterMilli)) -
            static_cast<int32_t>(push.jitterMilli / 2u);
        const int32_t slope =
            static_cast<int32_t>(scaled12(hk, push.slopeBiasMilli)) -
            static_cast<int32_t>(push.slopeBiasMilli / 2u);
        const int32_t windSign =
            static_cast<int32_t>((hk >> 16u) & 1023u) - 511;
        const uint32_t windMagnitude =
            (static_cast<uint32_t>(std::abs(windSign)) * push.windMilli) /
            1000000u;
        const int32_t windHalf =
            windSign < 0 ? -static_cast<int32_t>(windMagnitude / 2u)
                         : static_cast<int32_t>(windMagnitude / 2u);
        const int32_t wind =
            windSign < 0 ? -static_cast<int32_t>(windMagnitude)
                         : static_cast<int32_t>(windMagnitude);
        const int32_t xMilli =
            static_cast<int32_t>(tileColumn * kTileSize * 1000u) +
            static_cast<int32_t>(scaled12(hx, kTileSize * 1000u)) + wind;
        const int32_t zMilli =
            static_cast<int32_t>(tileRow * kTileSize * 1000u) +
            static_cast<int32_t>(scaled12(hz, kTileSize * 1000u)) - windHalf;
        const int32_t yMilli = static_cast<int32_t>(push.altitudeMilli) +
                               jitter + slope;
        Instance instance;
        instance.x = static_cast<float>(xMilli) * 0.001f;
        instance.y = static_cast<float>(yMilli) * 0.001f;
        instance.z = static_cast<float>(zMilli) * 0.001f;
        instance.kind = (hk >> 8u) & 3u;
        result.instances.push_back(instance);
    }
    result.count = static_cast<uint32_t>(result.instances.size());
    result.completed = true;
    result.hash = hashTile(result);
    return result;
}

TileData makeTileFromInstances(const Params& params, uint32_t tile, uint32_t count,
                               const std::vector<Instance>& instances) {
    TileData result;
    result.tile = tile;
    result.count = count;
    result.instances.assign(instances.begin(),
                            instances.begin() +
                                static_cast<std::ptrdiff_t>(std::min<size_t>(
                                    count, instances.size())));
    result.completed = !isMissingTile(params, tile);
    result.hash = hashTile(result);
    return result;
}

uint64_t hashTile(const TileData& tile) {
    if (tile.count == 0) {
        return 0;
    }
    uint64_t h = hashBytes(&tile.tile, sizeof(tile.tile));
    hashAppend(h, &tile.count, sizeof(tile.count));
    if (!tile.instances.empty()) {
        hashAppend(h, tile.instances.data(),
                   tile.instances.size() * sizeof(Instance));
    }
    return h;
}

uint64_t hashAggregate(const std::vector<TileData>& tiles) {
    uint64_t h = kFnvOffset;
    for (const TileData& tile : tiles) {
        hashAppend(h, &tile.count, sizeof(tile.count));
        hashAppend(h, &tile.hash, sizeof(tile.hash));
        if (!tile.instances.empty()) {
            hashAppend(h, tile.instances.data(),
                       tile.instances.size() * sizeof(Instance));
        }
    }
    return h;
}

BoundarySample sampleBoundary(const Params& params,
                              const std::vector<TileData>& tiles, uint32_t tile) {
    BoundarySample sample;
    sample.tile = tile;
    if (tile >= tiles.size() || !tiles[tile].completed) {
        return sample;
    }
    sample.valid = true;
    sample.acceptedCount = tiles[tile].count;
    sample.tileHash = tiles[tile].hash;
    if (!tiles[tile].instances.empty()) {
        const Instance& first = tiles[tile].instances.front();
        sample.firstPos = {first.x, first.y, first.z};
    }
    (void)params;
    return sample;
}

std::string hex64(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

std::string trimFloat(float value) {
    if (std::abs(value) < 0.00000005f) {
        value = 0.0f;
    }
    std::ostringstream out;
    out << std::setprecision(8) << value;
    return out.str();
}

}  // namespace vegd
