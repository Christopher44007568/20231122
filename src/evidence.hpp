#pragma once

#include "snapshot.hpp"

#include <string>

namespace skr {

// Plain PPM (P6) output: no external image dependency, trivially reviewable
// and convertible to PNG by any standard tool.
void writePpm(const std::string& path, const uint8_t* rgba,
              uint32_t width, uint32_t height);

// Side-by-side RGB diff visualization: legacy | current | amplified delta.
void writeDiffPpm(const std::string& path, const Snapshot& a,
                  const Snapshot& b);

struct ImageDelta {
    uint64_t differingPixels = 0;
    uint64_t totalPixels = 0;
    double meanAbsError = 0.0;   // average per-channel absolute error
    uint32_t maxChannelError = 0;
};

ImageDelta compareImages(const Snapshot& a, const Snapshot& b);

// JSON emitters for the review bundle.
std::string snapshotToJson(const Snapshot& s);
std::string correlationToJson(const Snapshot& legacy, const Snapshot& current,
                              const ImageDelta& delta,
                              const std::string& diffImagePath,
                              bool cpuGpuConsistent,
                              double cpuGpuMaxPositionError,
                              bool imageEquivalent,
                              const std::string& verdict,
                              const std::string& deltaAttribution);

} // namespace skr
