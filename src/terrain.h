#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace terrain {

constexpr int CHUNK_SIZE = 32; // 32x32 height samples per chunk

// Two frozen algorithm snapshots used as experimental controls.
enum class Snapshot { V1, V2 };

const char* snapshotName(Snapshot s);
Snapshot snapshotFromName(const std::string& name);

// Deterministic heightmap for chunk (cx, cy); identical inputs always
// produce identical bytes, which is what makes the experiment replayable.
std::vector<float> generateHeightmap(uint32_t seed, Snapshot s, int cx, int cy);

uint64_t fnv1a64(const void* data, size_t size);
std::string hex64(uint64_t v);

} // namespace terrain
