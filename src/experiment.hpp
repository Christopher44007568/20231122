#pragma once

#include "common.hpp"

#include <cstdint>

namespace inst {

struct ExperimentOptions {
    fs::path inputDir = "sample_input";
    fs::path outputDir = "artifacts/evidence";
    uint32_t shuffleSeed = 0x51A7E5u;
};

struct ExperimentSummary {
    bool pass = false;
    bool orderIndependent = false;
    bool legacyOrderSensitive = false;
    bool duplicateSuppressed = false;
    bool snapshotsDivergent = false;
    bool exceptionIsolated = false;
    bool cleanupOk = false;
    uint32_t loadedObjects = 0;
    uint32_t skippedObjects = 0;
    uint64_t acceptedRecords = 0;
    uint32_t selectedLegacy = 0;
    uint32_t selectedStable = 0;
    std::string legacyHash;
    std::string stableHash;
    uint64_t differingPixels = 0;
};

// Runs the full experiment. Returns 0 only if every required invariant holds.
int runExperiment(const ExperimentOptions& options);

// Rebuilds all selection states from the preserved evidence inputs and checks
// them against every replayable step in the trace. Returns mismatch count.
int replayTrace(const fs::path& tracePath, const fs::path& reportPath);

} // namespace inst
