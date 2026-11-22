#pragma once
#include "vk_context.h"
#include "terrain.h"
#include "journal.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

// Extension deliberately requested by the fault-injection probe step.
// It is not implemented by any known driver, so device creation with it
// must fail with VK_ERROR_EXTENSION_NOT_PRESENT.
constexpr const char* kUnknownExtension = "VK_EXT_terrain_unknown_probe";

struct RunConfig {
    terrain::Snapshot snapshot = terrain::Snapshot::V1;
    std::string order = "row";   // "row" | "shuffled"
    uint32_t seed = 20231122;
    int gridW = 4;
    int gridH = 4;
    int poisonIndex = 7;         // order index triggering the unknown-extension probe
    std::string runDir;          // evidence output dir for this run
};

struct RunResult {
    std::string finalStateHash;
    uint64_t totalBytes = 0;
    int loaded = 0;
    int duplicates = 0;
    int exceptions = 0;
    bool cleanupOk = false;
};

using ChunkMap = std::map<std::pair<int, int>, std::string>; // (cx,cy) -> data hash hex

std::string aggregateStateHash(const ChunkMap& chunks, uint64_t totalBytes);

class TerrainLoader {
public:
    TerrainLoader(VkContext& vk, terrain::Snapshot snap, uint32_t seed);
    // Generates, uploads to Vulkan, reads back and verifies. Returns data hash hex.
    std::string loadChunk(int cx, int cy, const std::string& tmpDir);
    bool isLoaded(int cx, int cy) const;
    std::string stateHash() const;
    uint64_t totalBytes() const { return totalBytes_; }
    int loadedCount() const { return static_cast<int>(chunks_.size()); }
    const ChunkMap& chunks() const { return chunks_; }
private:
    VkContext& vk_;
    terrain::Snapshot snap_;
    uint32_t seed_;
    ChunkMap chunks_;
    uint64_t totalBytes_ = 0;
};

RunResult runLoading(VkContext& vk, const RunConfig& cfg);

// Re-executes every recorded chunk_load from the journal and verifies hashes.
// Returns mismatch count; writes a JSON report to reportPath.
int replayJournal(VkContext& vk, const std::string& journalPath, const std::string& reportPath);
