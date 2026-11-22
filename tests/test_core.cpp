#include "state.hpp"
#include "vegetation.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<vegd::TileData> generateAll(const vegd::Params& params,
                                        uint32_t capacity) {
    std::vector<vegd::TileData> tiles(vegd::kTileCount);
    for (uint32_t tile = 0; tile < vegd::kTileCount; ++tile) {
        tiles[tile] = vegd::generateTileCpu(params, tile, capacity);
    }
    return tiles;
}

void testGenerationAndGap() {
    const vegd::Params params = vegd::Params::extreme();
    const auto tiles = generateAll(params, vegd::kMaxInstancesPerTile);
    uint64_t total = 0;
    for (uint32_t tile = 0; tile < vegd::kTileCount; ++tile) {
        total += tiles[tile].count;
        if (vegd::isMissingTile(params, tile)) {
            expect(tiles[tile].count == 0, "missing segment must be empty");
        } else {
            expect(tiles[tile].count > 0, "generated tile must not be empty");
        }
    }
    expect(total > 100000, "extreme parameter set should be dense");
    const auto before =
        vegd::sampleBoundary(params, tiles, params.missingStart - 1);
    const auto after =
        vegd::sampleBoundary(params, tiles, params.missingEnd);
    expect(before.valid && before.acceptedCount > 0,
           "before-boundary sample must be valid");
    expect(after.valid && after.acceptedCount > 0,
           "after-boundary sample must be valid");
    expect(before.tileHash != after.tileHash,
           "boundary samples should be distinguishable");
}

void testCheckpointRoundTrip() {
    const vegd::Params params = vegd::Params::extreme();
    const vegd::ResourcePlan plan =
        vegd::planResources(params, vegd::kDefaultBudgetBytes);
    auto tiles = generateAll(params, plan.capacity);
    std::vector<vegd::TileData> prefix = tiles;
    for (uint32_t tile = params.missingStart; tile < vegd::kTileCount; ++tile) {
        prefix[tile] = vegd::TileData{};
        prefix[tile].tile = tile;
    }
    vegd::Checkpoint checkpoint =
        vegd::makeEmptyCheckpoint(params, plan.capacity, plan.budgetBytes);
    for (uint32_t tile = 0; tile < params.missingStart; ++tile) {
        vegd::applyTile(checkpoint, prefix[tile]);
    }
    checkpoint.nextTile = params.missingEnd;
    checkpoint.aggregateHash = vegd::hashAggregate(prefix);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "vegd-test-state.bin";
    const vegd::StateIoResult saved =
        vegd::saveCheckpoint(path.string(), checkpoint);
    expect(saved.ok, "checkpoint should save");
    expect(saved.bytes > 600000, "checkpoint should retain dense tile data");

    const vegd::StateLoadResult loaded =
        vegd::loadCheckpoint(path.string());
    expect(loaded.ok, "checkpoint should load");
    expect(loaded.checksum == saved.checksum,
           "checkpoint checksum should be stable");
    expect(loaded.checkpoint.nextTile == params.missingEnd,
           "checkpoint next tile should survive");
    expect(loaded.checkpoint.completedMask == checkpoint.completedMask,
           "checkpoint completion mask should survive");
    const vegd::BoundarySample beforeLoad = vegd::sampleBoundary(
        params, vegd::checkpointTiles(loaded.checkpoint),
        params.missingStart - 1);
    const vegd::BoundarySample beforeOriginal = vegd::sampleBoundary(
        params, prefix, params.missingStart - 1);
    expect(beforeLoad.tileHash == beforeOriginal.tileHash,
           "persisted boundary hash should match");
    expect(beforeLoad.firstPos.x == beforeOriginal.firstPos.x,
           "persisted boundary first position should match");

    std::filesystem::remove(path);
}

void testCorruptCheckpointRejected() {
    const vegd::Params params = vegd::Params::extreme();
    vegd::Checkpoint checkpoint =
        vegd::makeEmptyCheckpoint(params, 512, 1024 * 1024);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "vegd-test-corrupt.bin";
    const vegd::StateIoResult saved =
        vegd::saveCheckpoint(path.string(), checkpoint);
    expect(saved.ok, "empty checkpoint should save");
    {
        std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
        stream.seekp(64);
        const char byte = static_cast<char>(0xFF);
        stream.write(&byte, 1);
    }
    const vegd::StateLoadResult loaded =
        vegd::loadCheckpoint(path.string());
    expect(!loaded.ok, "corrupt checkpoint must be rejected");
    std::filesystem::remove(path);
}

void testResumeMatchesFull() {
    const vegd::Params params = vegd::Params::extreme();
    const vegd::ResourcePlan plan = vegd::planResources(params, 256 * 1024);
    expect(plan.capacity < params.maxInstancesPerTile,
           "low budget should clamp capacity");
    expect(!plan.degradations.empty(),
           "low budget should record a degradation");
    const auto full = generateAll(params, plan.capacity);
    vegd::Checkpoint checkpoint =
        vegd::makeEmptyCheckpoint(params, plan.capacity, plan.budgetBytes);
    for (uint32_t tile = 0; tile < params.missingStart; ++tile) {
        vegd::applyTile(checkpoint, full[tile]);
    }
    checkpoint.nextTile = params.missingEnd;
    auto resumed = vegd::checkpointTiles(checkpoint);
    for (uint32_t tile = checkpoint.nextTile; tile < vegd::kTileCount; ++tile) {
        resumed[tile] = vegd::generateTileCpu(params, tile, plan.capacity);
    }
    expect(vegd::hashAggregate(full) == vegd::hashAggregate(resumed),
           "resumed aggregate hash must equal full run");
    const auto fullBefore =
        vegd::sampleBoundary(params, full, params.missingStart - 1);
    const auto resumedBefore =
        vegd::sampleBoundary(params, resumed, params.missingStart - 1);
    const auto fullAfter =
        vegd::sampleBoundary(params, full, params.missingEnd);
    const auto resumedAfter =
        vegd::sampleBoundary(params, resumed, params.missingEnd);
    expect(fullBefore.tileHash == resumedBefore.tileHash,
           "before-boundary tile hash must be unchanged");
    expect(fullAfter.tileHash == resumedAfter.tileHash,
           "after-boundary tile hash must be unchanged");
}

}  // namespace

int main() {
    testGenerationAndGap();
    testCheckpointRoundTrip();
    testCorruptCheckpointRejected();
    testResumeMatchesFull();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all core tests passed\n";
    return 0;
}
