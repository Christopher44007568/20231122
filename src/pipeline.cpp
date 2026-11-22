#include "pipeline.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

#include "artifact.h"
#include "scene.h"
#include "sha256.h"
#include "util.h"

namespace {

void appendRuntimeEvent(std::vector<ResourceEvent>& events,
                        const std::string& stage,
                        const std::string& severity,
                        const std::string& code,
                        uint64_t requested,
                        uint64_t available,
                        const std::string& action,
                        const std::string& detail) {
    ResourceEvent event;
    event.sequence = static_cast<int>(events.size());
    event.stage = stage;
    event.severity = severity;
    event.code = code;
    event.requestedBytes = requested;
    event.availableBytes = available;
    event.action = action;
    event.detail = detail;
    events.push_back(std::move(event));
}

FrameRecord writeFrame(const std::string& directory,
                       const CaptureObservation& observation,
                       const Image& image) {
    std::string relative = frameFileName(observation.taskIndex);
    std::string path = joinPath(directory, relative);
    writePpm(path, image);
    FrameRecord record;
    record.taskIndex = observation.taskIndex;
    record.sequence = observation.sequence;
    record.deviceIndex = observation.deviceIndex;
    record.deviceId = observation.deviceIndex == 0 ? "cam-alpha" : "cam-beta";
    record.estimatedReferenceNs = observation.estimatedReferenceNs;
    record.file = relative;
    record.sha256 = sha256File(path);
    record.bytes = static_cast<uint64_t>(std::filesystem::file_size(path));
    return record;
}

void validateSeedFrames(const RunOptions& options, int startTask) {
    int expectedTask = 0;
    for (const FrameRecord& record : options.seededFrames) {
        if (record.taskIndex != expectedTask) {
            throw std::runtime_error("checkpoint frame order is not contiguous at task " +
                                     std::to_string(expectedTask));
        }
        if (record.taskIndex >= startTask) {
            throw std::runtime_error("checkpoint contains frames beyond its resume point");
        }
        ++expectedTask;
    }
    if (expectedTask != startTask) {
        throw std::runtime_error("checkpoint frame count does not match next task index");
    }
}

}  // namespace

std::string frameFileName(int taskIndex) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "frames/task_%04d.ppm", taskIndex);
    return buffer;
}

RunResult executeRun(const Config& config,
                     const ClockModel& clock,
                     const RunOptions& options) {
    if (options.stopTask < options.startTask || options.stopTask < 0 ||
        options.stopTask > static_cast<int>(clock.observations.size())) {
        throw std::runtime_error("invalid task range for run");
    }
    validateSeedFrames(options, options.startTask);
    ensureDirectory(options.directory);
    ensureDirectory(joinPath(options.directory, "frames"));

    RunResult result;
    result.config = config;
    result.startedUtc = utcTimestamp();
    result.events = options.initialEvents;
    result.resumedFromTask = options.resumedFromTask;
    result.checkpointPath = options.checkpointPath;
    result.checkpointSha256 = options.checkpointSha256;
    result.plan = options.fixedPlan ? *options.fixedPlan
                                    : makeRenderPlan(config, config.memoryBudgetBytes);

    for (const FrameRecord& record : options.seededFrames) {
        std::string source = joinPath(options.seedFrameDirectory, record.file);
        std::string destination = joinPath(options.directory, record.file);
        if (sha256File(source) != record.sha256) {
            throw std::runtime_error("checkpoint frame hash mismatch: " + source);
        }
        copyFile(source, destination);
        if (sha256File(destination) != record.sha256) {
            throw std::runtime_error("copied checkpoint frame hash mismatch: " + destination);
        }
        result.frames.push_back(record);
    }

    std::unique_ptr<Renderer> renderer = createRenderer(config, result.plan, result.events);
    result.diagnostics = renderer->diagnostics();
    result.diagnostics.set("selected_backend", renderer->backendName());

    for (int task = options.startTask; task < options.stopTask; ++task) {
        const CaptureObservation& observation = clock.observations[static_cast<size_t>(task)];
        RenderParams params = makeRenderParams(config, observation,
                                               static_cast<int>(clock.observations.size()));
        Image image;
        std::string renderError;
        bool rendered = renderer->renderFrame(params, image, renderError);
        if (!rendered && renderer->backendName() != "cpu-reference") {
            appendRuntimeEvent(result.events, "render", "error", "compute_dispatch_failed",
                               estimateTransientBytes(config), result.plan.budgetBytes,
                               "cpu-reference-retry", renderError);
            result.plan.effectiveBackend = "cpu-reference";
            result.plan.degraded = true;
            result.plan.reason = "runtime compute failure; retried with CPU reference";
            renderer = createCpuRenderer();
            rendered = renderer->renderFrame(params, image, renderError);
        }
        if (!rendered) {
            throw std::runtime_error("frame render failed at task " +
                                     std::to_string(task) + ": " + renderError);
        }
        result.frames.push_back(writeFrame(options.directory, observation, image));
    }

    result.nextTaskIndex = options.stopTask;
    result.finishedUtc = utcTimestamp();
    result.paused = options.saveCheckpoint && options.stopTask < static_cast<int>(clock.observations.size());
    if (options.saveCheckpoint) {
        result.checkpointPath = joinPath(options.directory, "checkpoint.json");
        writeCheckpoint(result.checkpointPath, result, clock);
        result.checkpointSha256 = sha256File(result.checkpointPath);
    }
    if (result.frames.empty()) throw std::runtime_error("run produced no frames");
    return result;
}

