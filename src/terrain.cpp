#include "terrain.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace terrain {

const char* snapshotName(Snapshot s) {
    return s == Snapshot::V1 ? "v1" : "v2";
}

Snapshot snapshotFromName(const std::string& name) {
    if (name == "v1") return Snapshot::V1;
    if (name == "v2") return Snapshot::V2;
    throw std::invalid_argument("unknown snapshot: " + name);
}

static float octave(float gx, float gy, float freq, float phase) {
    return std::sin(gx * freq + phase) * std::cos(gy * freq + phase);
}

std::vector<float> generateHeightmap(uint32_t seed, Snapshot s, int cx, int cy) {
    std::vector<float> out(CHUNK_SIZE * CHUNK_SIZE);
    const float baseX = static_cast<float>(cx * CHUNK_SIZE);
    const float baseY = static_cast<float>(cy * CHUNK_SIZE);
    const float fs = static_cast<float>(seed);
    for (int y = 0; y < CHUNK_SIZE; ++y) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            const float gx = baseX + static_cast<float>(x);
            const float gy = baseY + static_cast<float>(y);
            float h;
            if (s == Snapshot::V1) {
                // snapshot v1: two octaves, coarse weights
                h = 0.6f * octave(gx, gy, 0.15f, fs * 0.010f)
                  + 0.3f * octave(gx, gy, 0.45f, fs * 0.020f)
                  + 0.1f;
            } else {
                // snapshot v2: three octaves, retuned weights/frequencies
                h = 0.5f * octave(gx, gy, 0.12f, fs * 0.013f)
                  + 0.3f * octave(gx, gy, 0.35f, fs * 0.070f)
                  + 0.2f * octave(gx, gy, 0.90f, fs * 0.110f);
            }
            out[static_cast<size_t>(y) * CHUNK_SIZE + x] = h;
        }
    }
    return out;
}

uint64_t fnv1a64(const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

std::string hex64(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return std::string(buf);
}

} // namespace terrain
