#pragma once

#include <string>
#include <vector>

#include "model.h"
#include "pipeline.h"

struct CheckpointData {
    Config config;
    RenderPlan plan;
    std::vector<ResourceEvent> events;
    std::vector<FrameRecord> frames;
    int nextTaskIndex = 0;
    Json raw = Json::object();
    std::string sha256;
};

CheckpointData loadCheckpoint(const std::string& path);

Json buildManifest(const RunResult& result,
                   const ClockModel& clock,
                   const std::string& mode,
                   bool resumed,
                   const std::string& checkpointDisplayPath,
                   const std::string& checkpointSha256,
                   int resumedFromTask);

void writeCheckpoint(const std::string& path,
                     const RunResult& result,
                     const ClockModel& clock);

Json buildComparison(const std::string& baselineDirectory,
                     const std::string& resumedDirectory,
                     const std::vector<FrameRecord>& baselineFrames,
                     const std::vector<FrameRecord>& resumedFrames);

void writeEvidence(const std::string& root,
                   const RunResult& baseline,
                   const RunResult& paused,
                   const RunResult& resumed,
                   const std::vector<RunResult>& pressureRuns,
                   const ClockModel& clock,
                   const std::string& commandLine,
                   bool comparisonPassed);

bool verifyArtifactRoot(const std::string& root, std::string& report);
void printArtifactSummary(const std::string& root);

