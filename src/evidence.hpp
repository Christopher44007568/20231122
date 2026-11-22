#pragma once

#include "checkpoint.hpp"
#include "config.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct EvidenceFrame {
    ImageMetrics metrics;
    std::array<uint32_t, 4> gpuStats{};
};

struct EvidenceRun {
    std::string mode;
    std::string timestampUtc;
    FogConfig config;
    MemoryPlan plan;
    std::string deviceName;
    std::string deviceType;
    uint64_t deviceMemoryBytes = 0;
    uint64_t memoryBudgetBytes = 0;
    bool simulatedBudget = false;
    uint64_t boundaryFrameBefore = 0;
    uint64_t boundaryFrameAfter = 0;
    CheckpointData checkpoint;
    ImageData beforeImage;
    ImageData afterImage;
    ImageData replayImage;
    ImageData continuationImage;
    EvidenceFrame beforeFrame;
    EvidenceFrame afterFrame;
    EvidenceFrame replayFrame;
    EvidenceFrame continuationFrame;
    BoundaryComparison boundaryChange;
    BoundaryComparison replayChange;
    bool replayExact = false;
    std::string status = "review";
    std::vector<std::string> warnings;
};

void writeEvidenceBundle(const std::string& directory, EvidenceRun& run);
std::string utcTimestamp();
