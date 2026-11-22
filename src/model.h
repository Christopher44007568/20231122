#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "json.h"

struct Config {
    int width = 320;
    int height = 200;
    int frames = 12;
    int pauseAfterTasks = 7;
    uint64_t memoryBudgetBytes = 24ULL * 1024ULL * 1024ULL;
    uint64_t pressureBudgetBytes = 32ULL * 1024ULL;
    std::string backend = "auto";
    std::string outputDirectory = "artifacts/demo";
    uint32_t seed = 20231122U;
    bool runPressureProbes = true;
    bool preview = false;
};

struct DeviceSpec {
    std::string id;
    std::string role;
    int64_t offsetNs = 0;
    double driftPpm = 0.0;
};

struct DeviceCalibration {
    DeviceSpec spec;
    int64_t probeReferenceNs[2] = {0, 0};
    int64_t probeDeviceNs[2] = {0, 0};
    double referenceNsPerDeviceNs = 1.0;
    double deviceNsPerReferenceNs = 1.0;
    int64_t originReferenceNs = 0;
    int64_t originDeviceNs = 0;
    int64_t maxAbsResidualNs = 0;
};

struct CaptureObservation {
    int taskIndex = 0;
    int sequence = 0;
    int deviceIndex = 0;
    int64_t referenceNs = 0;
    int64_t deviceTimeNs = 0;
    int64_t estimatedReferenceNs = 0;
    int64_t residualNs = 0;
    std::string objectUid;
};

struct ClockModel {
    std::vector<DeviceCalibration> devices;
    std::vector<CaptureObservation> observations;
    int64_t betaMinusAlphaOffsetNs = 0;
    double betaMinusAlphaDriftPpm = 0.0;
};

struct RenderPlan {
    std::string requestedBackend = "auto";
    std::string effectiveBackend = "cpu-reference";
    bool degraded = false;
    std::string reason = "not planned";
    uint64_t budgetBytes = 0;
    uint64_t fullTransientBytes = 0;
    uint32_t bandHeight = 0;
    uint32_t bandCount = 0;
};

struct ResourceEvent {
    int sequence = 0;
    std::string stage;
    std::string severity;
    std::string code;
    uint64_t requestedBytes = 0;
    uint64_t availableBytes = 0;
    std::string action;
    std::string detail;
};

struct RenderParams {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t tileY = 0;
    uint32_t tileHeight = 0;
    uint32_t frameIndex = 0;
    uint32_t totalFrames = 0;
    uint32_t deviceIndex = 0;
    uint32_t seed = 0;
    float timeSeconds = 0.0f;
    float focusDepth = 0.54f;
    float aperture = 0.115f;
    float phase = 0.0f;
    float clockOffsetMs = 0.0f;
    float clockDriftPpm = 0.0f;
    float exposure = 1.0f;
    float padding = 0.0f;
};

struct FrameRecord {
    int taskIndex = 0;
    int sequence = 0;
    int deviceIndex = 0;
    std::string deviceId;
    int64_t estimatedReferenceNs = 0;
    std::string file;
    std::string sha256;
    uint64_t bytes = 0;
};

Config defaultConfig();
Json configToJson(const Config& config);
Json renderPlanToJson(const RenderPlan& plan);
Json resourceEventToJson(const ResourceEvent& event);

