#pragma once

#include <string>
#include <vector>

#include "clock.h"
#include "model.h"
#include "renderer.h"

struct RunOptions {
    std::string directory;
    std::string mode;
    int startTask = 0;
    int stopTask = 0;
    bool saveCheckpoint = false;
    bool resume = false;
    std::string checkpointPath;
    std::string checkpointSha256;
    int resumedFromTask = 0;
    std::vector<ResourceEvent> initialEvents;
    const RenderPlan* fixedPlan = nullptr;
    std::vector<FrameRecord> seededFrames;
    std::string seedFrameDirectory;
};

struct RunResult {
    Config config;
    RenderPlan plan;
    std::vector<ResourceEvent> events;
    std::vector<FrameRecord> frames;
    Json diagnostics = Json::object();
    int nextTaskIndex = 0;
    bool paused = false;
    std::string checkpointPath;
    std::string checkpointSha256;
    int resumedFromTask = 0;
    std::string startedUtc;
    std::string finishedUtc;
};

RunResult executeRun(const Config& config,
                     const ClockModel& clock,
                     const RunOptions& options);

std::string frameFileName(int taskIndex);

