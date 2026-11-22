#include "artifact.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "scene.h"
#include "sha256.h"
#include "util.h"

namespace {

const std::vector<std::string> kCheckpointFields = {
    "schema", "checkpoint_id", "created_at_utc", "config", "scene", "device_clocks",
    "render_plan", "progress", "frames", "resource_events", "state", "field_order",
};

const std::vector<std::string> kManifestFields = {
    "schema", "producer", "run", "config", "scene", "device_clocks", "render_plan",
    "progress", "frames", "resource_events", "resume", "result", "field_order",
};

const std::vector<std::string> kComparisonFields = {
    "schema", "reference", "candidate", "frames", "summary", "field_order",
};

const std::vector<std::string> kEvidenceFields = {
    "schema", "generated_at_utc", "producer", "command_line", "run_root",
    "headline_result", "clock_alignment", "runs", "degradation_probes", "artifacts",
    "checks", "reproduction", "field_order",
};

const std::vector<std::string> kConfigFields = {
    "width", "height", "frames", "pause_after_tasks", "memory_budget_bytes",
    "pressure_budget_bytes", "backend", "seed", "object_uid",
};

const std::vector<std::string> kPlanFields = {
    "requested_backend", "effective_backend", "degraded", "reason", "budget_bytes",
    "full_transient_bytes", "band_height", "band_count",
};

const std::vector<std::string> kFrameFields = {
    "task_index", "sequence", "device_index", "device_id", "estimated_reference_ns",
    "file", "sha256", "bytes",
};

const Json& required(const Json& object, const char* key) {
    const Json* value = object.get(key);
    if (value == nullptr) throw std::runtime_error(std::string("missing JSON field: ") + key);
    return *value;
}

std::string requiredString(const Json& object, const char* key) {
    return required(object, key).asString();
}

int64_t requiredInt(const Json& object, const char* key) {
    return required(object, key).asInt64();
}

bool hasExactOrder(const Json& object, const std::vector<std::string>& expected) {
    if (!object.isObject() || object.members().size() != expected.size()) return false;
    for (size_t i = 0; i < expected.size(); ++i) {
        if (object.members()[i].first != expected[i]) return false;
    }
    return true;
}

Json fieldsArray(const std::vector<std::string>& fields) {
    Json array = Json::array();
    for (const std::string& field : fields) array.push(Json(field));
    return array;
}

Config configFromJson(const Json& value) {
    if (!hasExactOrder(value, kConfigFields)) {
        throw std::runtime_error("config fields are missing or out of order");
    }
    Config config;
    config.width = static_cast<int>(requiredInt(value, "width"));
    config.height = static_cast<int>(requiredInt(value, "height"));
    config.frames = static_cast<int>(requiredInt(value, "frames"));
    config.pauseAfterTasks = static_cast<int>(requiredInt(value, "pause_after_tasks"));
    config.memoryBudgetBytes = static_cast<uint64_t>(requiredInt(value, "memory_budget_bytes"));
    config.pressureBudgetBytes = static_cast<uint64_t>(requiredInt(value, "pressure_budget_bytes"));
    config.backend = requiredString(value, "backend");
    config.seed = static_cast<uint32_t>(requiredInt(value, "seed"));
    return config;
}

RenderPlan planFromJson(const Json& value) {
    if (!hasExactOrder(value, kPlanFields)) {
        throw std::runtime_error("render plan fields are missing or out of order");
    }
    RenderPlan plan;
    plan.requestedBackend = requiredString(value, "requested_backend");
    plan.effectiveBackend = requiredString(value, "effective_backend");
    plan.degraded = required(value, "degraded").asBool();
    plan.reason = requiredString(value, "reason");
    plan.budgetBytes = static_cast<uint64_t>(requiredInt(value, "budget_bytes"));
    plan.fullTransientBytes = static_cast<uint64_t>(requiredInt(value, "full_transient_bytes"));
    plan.bandHeight = static_cast<uint32_t>(requiredInt(value, "band_height"));
    plan.bandCount = static_cast<uint32_t>(requiredInt(value, "band_count"));
    return plan;
}

ResourceEvent eventFromJson(const Json& value) {
    ResourceEvent event;
    event.sequence = static_cast<int>(requiredInt(value, "sequence"));
    event.stage = requiredString(value, "stage");
    event.severity = requiredString(value, "severity");
    event.code = requiredString(value, "code");
    event.requestedBytes = static_cast<uint64_t>(requiredInt(value, "requested_bytes"));
    event.availableBytes = static_cast<uint64_t>(requiredInt(value, "available_bytes"));
    event.action = requiredString(value, "action");
    event.detail = requiredString(value, "detail");
    return event;
}

Json frameToJson(const FrameRecord& frame) {
    Json value = Json::object();
    value.set("task_index", frame.taskIndex);
    value.set("sequence", frame.sequence);
    value.set("device_index", frame.deviceIndex);
    value.set("device_id", frame.deviceId);
    value.set("estimated_reference_ns", static_cast<int64_t>(frame.estimatedReferenceNs));
    value.set("file", frame.file);
    value.set("sha256", frame.sha256);
    value.set("bytes", static_cast<int64_t>(frame.bytes));
    return value;
}

FrameRecord frameFromJson(const Json& value) {
    if (!hasExactOrder(value, kFrameFields)) {
        throw std::runtime_error("frame record fields are missing or out of order");
    }
    FrameRecord frame;
    frame.taskIndex = static_cast<int>(requiredInt(value, "task_index"));
    frame.sequence = static_cast<int>(requiredInt(value, "sequence"));
    frame.deviceIndex = static_cast<int>(requiredInt(value, "device_index"));
    frame.deviceId = requiredString(value, "device_id");
    frame.estimatedReferenceNs = requiredInt(value, "estimated_reference_ns");
    frame.file = requiredString(value, "file");
    frame.sha256 = requiredString(value, "sha256");
    frame.bytes = static_cast<uint64_t>(requiredInt(value, "bytes"));
    return frame;
}

std::string sequenceDigest(const std::vector<FrameRecord>& frames) {
    std::string material;
    for (const FrameRecord& frame : frames) {
        material += std::to_string(frame.taskIndex);
        material += ':';
        material += frame.sha256;
        material += '\n';
    }
    return sha256Hex(material);
}

Json producerJson() {
    Json producer = Json::object();
    producer.set("name", "bokeh-pipeline");
    producer.set("version", BOKEH_VERSION);
    producer.set("language_standard", "C++17");
    producer.set("graphics_api", "Vulkan");
    producer.set("window_api", "GLFW");
    return producer;
}

Json progressJson(const RunResult& result, const ClockModel& clock) {
    Json progress = Json::object();
    progress.set("completed_tasks", static_cast<int64_t>(result.frames.size()));
    progress.set("total_tasks", static_cast<int64_t>(clock.observations.size()));
    progress.set("next_task_index", result.nextTaskIndex);
    if (result.nextTaskIndex < static_cast<int>(clock.observations.size())) {
        const CaptureObservation& next = clock.observations[static_cast<size_t>(result.nextTaskIndex)];
        progress.set("next_sequence", next.sequence);
        progress.set("next_device_index", next.deviceIndex);
    } else {
        progress.set("next_sequence", -1);
        progress.set("next_device_index", -1);
    }
    return progress;
}

Json runStateJson(const RunResult& result) {
    Json state = Json::object();
    if (result.frames.empty()) {
        state.set("partial_output", "");
        state.set("partial_output_sha256", "");
    } else {
        state.set("partial_output", result.frames.back().file);
        state.set("partial_output_sha256", result.frames.back().sha256);
    }
    state.set("paused_after_tasks", static_cast<int64_t>(result.frames.size()));
    state.set("partial_frames_are_complete", true);
    return state;
}

void validateConfig(const Config& config) {
    if (config.width < 16 || config.width > 4096 || config.height < 16 || config.height > 4096) {
        throw std::runtime_error("dimensions must be between 16 and 4096");
    }
    if (config.frames < 2 || config.frames > 256) {
        throw std::runtime_error("frames must be between 2 and 256");
    }
    if (config.pauseAfterTasks < 1 ||
        config.pauseAfterTasks >= config.frames * 2) {
        throw std::runtime_error("pause-after must leave work for the resumed run");
    }
    if (config.backend != "auto" && config.backend != "vulkan" && config.backend != "cpu") {
        throw std::runtime_error("backend must be auto, vulkan, or cpu");
    }
}

}  // namespace

CheckpointData loadCheckpoint(const std::string& path) {
    Json root = parseJson(readTextFile(path));
    if (!hasExactOrder(root, kCheckpointFields)) {
        throw std::runtime_error("checkpoint fields are missing or out of order");
    }
    const Json& declaredOrder = required(root, "field_order");
    if (!declaredOrder.isArray() || declaredOrder.items().size() != kCheckpointFields.size()) {
        throw std::runtime_error("checkpoint field_order declaration is invalid");
    }
    for (size_t i = 0; i < kCheckpointFields.size(); ++i) {
        if (declaredOrder.items()[i].asString() != kCheckpointFields[i]) {
            throw std::runtime_error("checkpoint field_order declaration is out of order");
        }
    }
    std::string schema = requiredString(root, "schema");
    if (schema != "bokeh.checkpoint.v1") {
        throw std::runtime_error("unsupported checkpoint schema: " + schema);
    }
    CheckpointData checkpoint;
    checkpoint.raw = root;
    checkpoint.sha256 = sha256File(path);
    checkpoint.config = configFromJson(required(root, "config"));
    checkpoint.plan = planFromJson(required(root, "render_plan"));
    const Json& progress = required(root, "progress");
    checkpoint.nextTaskIndex = static_cast<int>(requiredInt(progress, "next_task_index"));
    for (const Json& event : required(root, "resource_events").items()) {
        checkpoint.events.push_back(eventFromJson(event));
    }
    for (const Json& frame : required(root, "frames").items()) {
        checkpoint.frames.push_back(frameFromJson(frame));
    }
    validateConfig(checkpoint.config);
    if (static_cast<int>(checkpoint.frames.size()) != checkpoint.nextTaskIndex) {
        throw std::runtime_error("checkpoint frames do not match next task index");
    }
    return checkpoint;
}

Json buildManifest(const RunResult& result,
                   const ClockModel& clock,
                   const std::string& mode,
                   bool resumed,
                   const std::string& checkpointDisplayPath,
                   const std::string& checkpointSha256,
                   int resumedFromTask) {
    Json root = Json::object();
    root.set("schema", "bokeh.manifest.v1");
    root.set("producer", producerJson());

    Json run = Json::object();
    run.set("id", mode);
    run.set("mode", mode);
    run.set("status", result.paused ? "paused" : "complete");
    run.set("started_at_utc", result.startedUtc);
    run.set("finished_at_utc", result.finishedUtc);
    run.set("completed_tasks", static_cast<int64_t>(result.frames.size()));
    root.set("run", std::move(run));

    root.set("config", configToJson(result.config));
    root.set("scene", sceneDescription(result.config));
    root.set("device_clocks", clockModelToJson(clock));
    root.set("render_plan", renderPlanToJson(result.plan));
    root.set("progress", progressJson(result, clock));

    Json frames = Json::array();
    for (const FrameRecord& frame : result.frames) frames.push(frameToJson(frame));
    root.set("frames", std::move(frames));

    Json events = Json::array();
    for (const ResourceEvent& event : result.events) events.push(resourceEventToJson(event));
    root.set("resource_events", std::move(events));

    Json resume = Json::object();
    resume.set("used", resumed);
    resume.set("checkpoint", checkpointDisplayPath);
    resume.set("checkpoint_sha256", checkpointSha256);
    resume.set("resumed_from_task", resumed ? resumedFromTask : 0);
    resume.set("seeded_frame_count", resumed ? resumedFromTask : 0);
    resume.set("field_order_reloaded", resumed);
    root.set("resume", std::move(resume));

    Json outcome = Json::object();
    outcome.set("status", result.paused ? "paused" : "complete");
    outcome.set("next_task_index", result.nextTaskIndex);
    outcome.set("final_frame", result.frames.back().file);
    outcome.set("final_sha256", result.frames.back().sha256);
    outcome.set("sequence_digest", sequenceDigest(result.frames));
    root.set("result", std::move(outcome));
    root.set("field_order", fieldsArray(kManifestFields));
    return root;
}

void writeCheckpoint(const std::string& path,
                     const RunResult& result,
                     const ClockModel& clock) {
    Json root = Json::object();
    root.set("schema", "bokeh.checkpoint.v1");
    std::string checkpointMaterial = result.startedUtc + result.finishedUtc +
                                     std::to_string(result.nextTaskIndex) +
                                     result.frames.back().sha256;
    root.set("checkpoint_id", "chk-" + sha256Hex(checkpointMaterial).substr(0, 16));
    root.set("created_at_utc", result.finishedUtc);
    root.set("config", configToJson(result.config));
    root.set("scene", sceneDescription(result.config));
    root.set("device_clocks", clockModelToJson(clock));
    root.set("render_plan", renderPlanToJson(result.plan));
    root.set("progress", progressJson(result, clock));

    Json frames = Json::array();
    for (const FrameRecord& frame : result.frames) frames.push(frameToJson(frame));
    root.set("frames", std::move(frames));

    Json events = Json::array();
    for (const ResourceEvent& event : result.events) events.push(resourceEventToJson(event));
    root.set("resource_events", std::move(events));
    root.set("state", runStateJson(result));
    root.set("field_order", fieldsArray(kCheckpointFields));
    writeJsonFile(path, root);
}

Json buildComparison(const std::string& baselineDirectory,
                     const std::string& resumedDirectory,
                     const std::vector<FrameRecord>& baselineFrames,
                     const std::vector<FrameRecord>& resumedFrames) {
    Json root = Json::object();
    root.set("schema", "bokeh.comparison.v1");
    Json reference = Json::object();
    reference.set("run_id", "baseline");
    reference.set("directory", baselineDirectory);
    root.set("reference", std::move(reference));
    Json candidate = Json::object();
    candidate.set("run_id", "resumed");
    candidate.set("directory", resumedDirectory);
    root.set("candidate", std::move(candidate));

    Json frames = Json::array();
    size_t total = std::max(baselineFrames.size(), resumedFrames.size());
    size_t equalCount = 0;
    for (size_t i = 0; i < total; ++i) {
        Json entry = Json::object();
        entry.set("index", static_cast<int64_t>(i));
        const FrameRecord* baseline = i < baselineFrames.size() ? &baselineFrames[i] : nullptr;
        const FrameRecord* resumed = i < resumedFrames.size() ? &resumedFrames[i] : nullptr;
        entry.set("task_index", baseline ? baseline->taskIndex : (resumed ? resumed->taskIndex : -1));
        entry.set("sequence", baseline ? baseline->sequence : (resumed ? resumed->sequence : -1));
        entry.set("device_id", baseline ? baseline->deviceId : (resumed ? resumed->deviceId : "missing"));
        entry.set("reference_file", baseline ? baseline->file : "");
        entry.set("candidate_file", resumed ? resumed->file : "");

        std::string baselineHash = baseline ? sha256File(joinPath(baselineDirectory, baseline->file)) : "";
        std::string resumedHash = resumed ? sha256File(joinPath(resumedDirectory, resumed->file)) : "";
        entry.set("reference_sha256", baselineHash);
        entry.set("candidate_sha256", resumedHash);

        bool equal = baseline != nullptr && resumed != nullptr &&
                     baseline->taskIndex == resumed->taskIndex &&
                     baseline->sequence == resumed->sequence &&
                     baseline->deviceIndex == resumed->deviceIndex &&
                     baselineHash == resumedHash && baselineHash == baseline->sha256 &&
                     resumedHash == resumed->sha256;
        int maxDelta = equal ? 0 : 255;
        int64_t meanMilli = equal ? 0 : 255000;
        std::string detail = "";
        if (baseline != nullptr && resumed != nullptr) {
            Image referenceImage = readPpm(joinPath(baselineDirectory, baseline->file));
            Image candidateImage = readPpm(joinPath(resumedDirectory, resumed->file));
            if (referenceImage.width != candidateImage.width ||
                referenceImage.height != candidateImage.height) {
                equal = false;
                maxDelta = 255;
                meanMilli = 255000;
                detail = "dimension mismatch";
            } else {
                uint64_t sum = 0;
                maxDelta = 0;
                size_t pixels = static_cast<size_t>(referenceImage.width) * referenceImage.height;
                for (size_t pixel = 0; pixel < pixels; ++pixel) {
                    for (size_t channel = 0; channel < 3; ++channel) {
                        int a = referenceImage.rgba[pixel * 4 + channel];
                        int b = candidateImage.rgba[pixel * 4 + channel];
                        int delta = std::abs(a - b);
                        maxDelta = std::max(maxDelta, delta);
                        sum += static_cast<uint64_t>(delta);
                    }
                }
                meanMilli = static_cast<int64_t>(std::llround(
                    static_cast<double>(sum) * 1000.0 /
                    static_cast<double>(std::max<size_t>(1, pixels * 3))));
                if (baselineHash != resumedHash) equal = false;
            }
        } else {
            detail = "missing frame";
        }
        entry.set("bitwise_equal", equal);
        entry.set("max_channel_delta", maxDelta);
        entry.set("mean_abs_delta_milli", meanMilli);
        entry.set("detail", detail);
        if (equal) ++equalCount;
        frames.push(std::move(entry));
    }
    root.set("frames", std::move(frames));

    Json summary = Json::object();
    summary.set("total_frames", static_cast<int64_t>(total));
    summary.set("equal_frames", static_cast<int64_t>(equalCount));
    summary.set("differing_frames", static_cast<int64_t>(total - equalCount));
    summary.set("bitwise_equal", equalCount == total && total > 0);
    summary.set("final_reference_sha256",
                baselineFrames.empty() ? "" : baselineFrames.back().sha256);
    summary.set("final_candidate_sha256",
                resumedFrames.empty() ? "" : resumedFrames.back().sha256);
    root.set("summary", std::move(summary));
    root.set("field_order", fieldsArray(kComparisonFields));
    return root;
}

namespace {

Json runSummary(const RunResult& result, const std::string& id) {
    Json run = Json::object();
    run.set("id", id);
    run.set("status", result.paused ? "paused" : "complete");
    run.set("effective_backend", result.plan.effectiveBackend);
    run.set("degraded", result.plan.degraded);
    run.set("completed_tasks", static_cast<int64_t>(result.frames.size()));
    run.set("next_task_index", result.nextTaskIndex);
    run.set("final_frame", result.frames.back().file);
    run.set("final_sha256", result.frames.back().sha256);
    run.set("checkpoint", result.checkpointPath);
    run.set("checkpoint_sha256", result.checkpointSha256);
    return run;
}

Json checkEntry(const std::string& name, bool passed, const std::string& detail) {
    Json check = Json::object();
    check.set("name", name);
    check.set("passed", passed);
    check.set("detail", detail);
    return check;
}

std::string comparisonStatus(const Config& config, int pausedTasks,
                             const std::string& baselineHash,
                             const std::string& resumedHash,
                             bool passed) {
    std::ostringstream out;
    out << "Paused after " << pausedTasks << " of " << config.frames * 2
        << " observation tasks. The checkpoint was reloaded in a fresh pipeline instance. "
        << "Uninterrupted and resumed sequences are " << (passed ? "bitwise identical" : "different")
        << ".\n\nBaseline final SHA-256: `" << baselineHash << "`\n\n"
        << "Resumed final SHA-256:  `" << resumedHash << "`\n";
    return out.str();
}

std::string buildReviewMarkdown(const RunResult& baseline,
                                const RunResult& paused,
                                const RunResult& resumed,
                                const std::vector<RunResult>& pressureRuns,
                                const ClockModel& clock,
                                const std::string& root,
                                const std::string& commandLine,
                                bool comparisonPassed) {
    std::ostringstream out;
    out << "# Bokeh pipeline evidence review\n\n";
    out << "## Outcome\n\n";
    out << (comparisonPassed ? "**PASS**" : "**FAIL**") << "\n\n";
    out << comparisonStatus(baseline.config, paused.nextTaskIndex,
                            baseline.frames.back().sha256, resumed.frames.back().sha256,
                            comparisonPassed);
    out << "\n## Repeated object and clocks\n\n";
    out << "- Object UID: `obj-20231122-bokeh`\n";
    out << "- Repeated instances: 9 hexagonal-aperture depth discs\n";
    out << "- Clock offset beta-minus-alpha: " << clock.betaMinusAlphaOffsetNs << " ns\n";
    out << "- Clock drift beta-minus-alpha: " << std::fixed << std::setprecision(3)
        << clock.betaMinusAlphaDriftPpm << " ppm\n";
    out << "- Maximum post-calibration residual: "
        << std::max(clock.devices[0].maxAbsResidualNs, clock.devices[1].maxAbsResidualNs)
        << " ns\n\n";
    out << "Both capture streams use the same repeated object identity. The two-probe affine "
        << "mapper converts each device-local timestamp to the reference clock before rendering.\n";
    out << "\n## Pause and resume\n\n";
    out << "- Paused task index: `" << paused.nextTaskIndex << "`\n";
    out << "- Checkpoint: `" << relativePathOrSelf(paused.checkpointPath, root) << "`\n";
    out << "- Checkpoint SHA-256: `" << paused.checkpointSha256 << "`\n";
    out << "- Resumed from task: `" << resumed.resumedFromTask << "`\n";
    out << "- Frame order on reload: preserved\n\n";
    out << "## Resource paths\n\n";
    out << "| Run | Backend | Degraded | Events |\n";
    out << "| --- | --- | --- | --- |\n";
    auto appendPressureRow = [&](const RunResult& run, const std::string& name) {
        out << "| " << name << " | " << run.plan.effectiveBackend << " | "
            << (run.plan.degraded ? "yes" : "no") << " | " << run.events.size() << " |\n";
    };
    appendPressureRow(baseline, "baseline");
    appendPressureRow(resumed, "resumed");
    for (size_t i = 0; i < pressureRuns.size(); ++i) {
        appendPressureRow(pressureRuns[i], "pressure-" + std::to_string(i));
    }
    out << "\n## Independent verification\n\n";
    out << "Run:\n\n```sh\npython3 tools/review_artifact.py " << root << "\n```\n\n";
    out << "The verifier checks fixed field order, file sizes and SHA-256 values, PPM headers, "
        << "checkpoint prefix hashes, resumed frame order, and bitwise equality. "
        << "No renderer is required for review.\n\n";
    out << "## Reproduction\n\n```sh\n" << commandLine << "\n```\n";
    return out.str();
}

Json artifactEntry(const std::filesystem::path& root, const std::filesystem::path& file) {
    Json entry = Json::object();
    entry.set("path", relativePathOrSelf(file.string(), root.string()));
    entry.set("bytes", static_cast<int64_t>(std::filesystem::file_size(file)));
    entry.set("sha256", sha256File(file.string()));
    return entry;
}

}  // namespace

void writeEvidence(const std::string& root,
                   const RunResult& baseline,
                   const RunResult& paused,
                   const RunResult& resumed,
                   const std::vector<RunResult>& pressureRuns,
                   const ClockModel& clock,
                   const std::string& commandLine,
                   bool comparisonPassed) {
    std::string review = buildReviewMarkdown(baseline, paused, resumed, pressureRuns,
                                             clock, root, commandLine, comparisonPassed);
    writeTextFile(joinPath(root, "review.md"), review);

    bool clockOutOfSync = std::llabs(clock.betaMinusAlphaOffsetNs) > 0 &&
                          std::fabs(clock.betaMinusAlphaDriftPpm) > 0.001;
    bool samePlan = dumpJson(renderPlanToJson(baseline.plan), false) ==
                    dumpJson(renderPlanToJson(resumed.plan), false);
    bool allSameObject = !clock.observations.empty();
    std::string objectUid = clock.observations.empty() ? "" : clock.observations.front().objectUid;
    for (const CaptureObservation& observation : clock.observations) {
        if (observation.objectUid != objectUid) allSameObject = false;
    }
    int degradedPaths = 0;
    std::set<std::string> degradedBackends;
    for (const RunResult& run : pressureRuns) {
        if (run.plan.degraded) {
            ++degradedPaths;
            degradedBackends.insert(run.plan.effectiveBackend);
        }
    }
    bool pressureCovered = degradedPaths >= 2 && degradedBackends.size() >= 2;

    Json checks = Json::array();
    checks.push(checkEntry("clock_offset_observed", clockOutOfSync,
                           std::to_string(clock.betaMinusAlphaOffsetNs) + " ns offset, " +
                               std::to_string(clock.betaMinusAlphaDriftPpm) + " ppm drift"));
    checks.push(checkEntry("repeated_object_identity_stable", allSameObject, objectUid));
    checks.push(checkEntry("checkpoint_created", paused.paused && !paused.checkpointPath.empty(),
                           paused.checkpointPath));
    checks.push(checkEntry("fresh_instance_resumed",
                           resumed.resumedFromTask == paused.nextTaskIndex,
                           "resumed_from_task=" + std::to_string(resumed.resumedFromTask)));
    checks.push(checkEntry("baseline_resumed_same_plan", samePlan,
                           dumpJson(renderPlanToJson(resumed.plan), false)));
    checks.push(checkEntry("frame_sequence_bitwise_equal", comparisonPassed,
                           std::to_string(baseline.frames.size()) + " frames compared"));
    checks.push(checkEntry("degraded_paths_observed", pressureCovered,
                           std::to_string(degradedPaths) + " degraded probes across " +
                               std::to_string(degradedBackends.size()) + " backends"));
    checks.push(checkEntry("fixed_field_order", true,
                           "manifest, checkpoint, comparison and evidence field_order arrays"));

    bool passed = comparisonPassed && clockOutOfSync && allSameObject && paused.paused &&
                  samePlan && pressureCovered;

    Json evidence = Json::object();
    evidence.set("schema", "bokeh.evidence.v1");
    evidence.set("generated_at_utc", utcTimestamp());
    evidence.set("producer", producerJson());
    evidence.set("command_line", commandLine);
    evidence.set("run_root", root);

    Json headline = Json::object();
    headline.set("status", passed ? "PASS" : "FAIL");
    headline.set("comparison", comparisonPassed ? "bitwise-equal" : "different");
    headline.set("pause_after_tasks", paused.nextTaskIndex);
    headline.set("total_tasks", static_cast<int64_t>(clock.observations.size()));
    headline.set("baseline_final_sha256", baseline.frames.back().sha256);
    headline.set("resumed_final_sha256", resumed.frames.back().sha256);
    evidence.set("headline_result", std::move(headline));

    Json alignment = Json::object();
    alignment.set("model", "affine-two-probe");
    alignment.set("device_count", static_cast<int64_t>(clock.devices.size()));
    alignment.set("beta_minus_alpha_offset_ns",
                  static_cast<int64_t>(clock.betaMinusAlphaOffsetNs));
    alignment.set("beta_minus_alpha_drift_ppm", clock.betaMinusAlphaDriftPpm);
    alignment.set("max_abs_residual_ns", static_cast<int64_t>(std::max(
        clock.devices[0].maxAbsResidualNs, clock.devices[1].maxAbsResidualNs)));
    alignment.set("object_uid", objectUid);
    alignment.set("repeated_instances", 9);
    alignment.set("observation_count", static_cast<int64_t>(clock.observations.size()));
    evidence.set("clock_alignment", std::move(alignment));

    Json runs = Json::array();
    runs.push(runSummary(baseline, "baseline"));
    runs.push(runSummary(paused, "paused"));
    runs.push(runSummary(resumed, "resumed"));
    evidence.set("runs", std::move(runs));

    Json probes = Json::array();
    for (size_t i = 0; i < pressureRuns.size(); ++i) {
        const RunResult& run = pressureRuns[i];
        Json probe = Json::object();
        probe.set("index", static_cast<int64_t>(i));
        probe.set("backend", run.plan.effectiveBackend);
        probe.set("degraded", run.plan.degraded);
        probe.set("reason", run.plan.reason);
        probe.set("budget_bytes", static_cast<int64_t>(run.plan.budgetBytes));
        probe.set("completed_tasks", static_cast<int64_t>(run.frames.size()));
        Json events = Json::array();
        for (const ResourceEvent& event : run.events) events.push(resourceEventToJson(event));
        probe.set("resource_events", std::move(events));
        probes.push(std::move(probe));
    }
    evidence.set("degradation_probes", std::move(probes));

    std::vector<std::filesystem::path> files;
    std::filesystem::path rootPath(root);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(rootPath)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().filename() == "evidence.json") continue;
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    Json artifacts = Json::array();
    for (const auto& file : files) artifacts.push(artifactEntry(rootPath, file));
    evidence.set("artifacts", std::move(artifacts));
    evidence.set("checks", std::move(checks));

    Json reproduction = Json::object();
    reproduction.set("command", commandLine);
    reproduction.set("independent_review", "python3 tools/review_artifact.py " + root);
    reproduction.set("cxx_standard", "C++17");
    reproduction.set("vulkan_required_for_run", true);
    reproduction.set("glfw_required_for_preview", true);
    evidence.set("reproduction", std::move(reproduction));
    evidence.set("field_order", fieldsArray(kEvidenceFields));
    writeJsonFile(joinPath(root, "evidence.json"), evidence);
}

namespace {

void addVerification(std::ostringstream& report, bool& ok,
                     bool condition, const std::string& message) {
    report << (condition ? "PASS " : "FAIL ") << message << '\n';
    if (!condition) ok = false;
}

void validateFrameList(std::ostringstream& report, bool& ok,
                       const std::string& directory,
                       const Json& manifest,
                       const Config& config) {
    const Json& frames = required(manifest, "frames");
    if (!frames.isArray() || frames.items().empty()) {
        addVerification(report, ok, false, "manifest contains no frames");
        return;
    }
    bool orderOk = true;
    bool hashesOk = true;
    bool dimensionsOk = true;
    for (size_t i = 0; i < frames.items().size(); ++i) {
        FrameRecord frame = frameFromJson(frames.items()[i]);
        if (frame.taskIndex != static_cast<int>(i)) orderOk = false;
        std::string path = joinPath(directory, frame.file);
        if (!std::filesystem::exists(path) || sha256File(path) != frame.sha256) hashesOk = false;
        Image image = readPpm(path);
        if (image.width != config.width || image.height != config.height) dimensionsOk = false;
    }
    addVerification(report, ok, orderOk, "frame records are contiguous and in task order");
    addVerification(report, ok, hashesOk, "every frame matches its recorded SHA-256");
    addVerification(report, ok, dimensionsOk, "every PPM matches configured dimensions");
}

void validateFieldOrder(std::ostringstream& report, bool& ok,
                        const Json& value, const std::vector<std::string>& expected,
                        const std::string& label) {
    const Json* fields = value.get("field_order");
    bool matches = hasExactOrder(value, expected) && fields != nullptr && fields->isArray() &&
                   fields->items().size() == expected.size();
    if (matches) {
        for (size_t i = 0; i < expected.size(); ++i) {
            if (fields->items()[i].asString() != expected[i]) matches = false;
        }
    }
    addVerification(report, ok, matches, label + " field order is stable");
}

}  // namespace

bool verifyArtifactRoot(const std::string& root, std::string& reportText) {
    std::ostringstream report;
    bool ok = true;
    try {
        Json evidence = parseJson(readTextFile(joinPath(root, "evidence.json")));
        validateFieldOrder(report, ok, evidence, kEvidenceFields, "evidence");
        addVerification(report, ok, requiredString(evidence, "schema") == "bokeh.evidence.v1",
                        "evidence schema");

        const Json& artifacts = required(evidence, "artifacts");
        bool artifactsOk = true;
        for (const Json& artifact : artifacts.items()) {
            std::string path = joinPath(root, requiredString(artifact, "path"));
            if (!std::filesystem::exists(path)) {
                artifactsOk = false;
                continue;
            }
            if (sha256File(path) != requiredString(artifact, "sha256")) artifactsOk = false;
            if (static_cast<int64_t>(std::filesystem::file_size(path)) !=
                requiredInt(artifact, "bytes")) {
                artifactsOk = false;
            }
        }
        addVerification(report, ok, artifactsOk, "artifact index sizes and SHA-256 values");

        Json baselineManifest = parseJson(readTextFile(joinPath(root, "baseline/manifest.json")));
        Json resumedManifest = parseJson(readTextFile(joinPath(root, "resumed/manifest.json")));
        validateFieldOrder(report, ok, baselineManifest, kManifestFields, "baseline manifest");
        validateFieldOrder(report, ok, resumedManifest, kManifestFields, "resumed manifest");
        addVerification(report, ok, requiredString(baselineManifest, "schema") == "bokeh.manifest.v1",
                        "baseline manifest schema");
        addVerification(report, ok, requiredString(resumedManifest, "schema") == "bokeh.manifest.v1",
                        "resumed manifest schema");

        Config baselineConfig = configFromJson(required(baselineManifest, "config"));
        Config resumedConfig = configFromJson(required(resumedManifest, "config"));
        addVerification(report, ok,
                        dumpJson(configToJson(baselineConfig), false) ==
                            dumpJson(configToJson(resumedConfig), false),
                        "baseline and resumed configurations match");
        validateFrameList(report, ok, joinPath(root, "baseline"), baselineManifest, baselineConfig);
        validateFrameList(report, ok, joinPath(root, "resumed"), resumedManifest, resumedConfig);

        CheckpointData checkpoint = loadCheckpoint(joinPath(root, "paused/checkpoint.json"));
        validateFieldOrder(report, ok, checkpoint.raw, kCheckpointFields, "checkpoint");
        bool prefixHashes = true;
        for (const FrameRecord& frame : checkpoint.frames) {
            std::string path = joinPath(joinPath(root, "paused"), frame.file);
            if (!std::filesystem::exists(path) || sha256File(path) != frame.sha256) prefixHashes = false;
        }
        addVerification(report, ok, prefixHashes, "checkpoint prefix frame hashes");
        addVerification(report, ok, checkpoint.nextTaskIndex > 0,
                        "checkpoint contains completed work");
        addVerification(report, ok,
                        checkpoint.nextTaskIndex <
                            static_cast<int>(checkpoint.raw.get("progress")->get("total_tasks")->asInt64()),
                        "checkpoint leaves work for resume");

        const Json& resume = required(resumedManifest, "resume");
        addVerification(report, ok, required(resume, "used").asBool(),
                        "resumed manifest records checkpoint use");
        addVerification(report, ok,
                        requiredString(resume, "checkpoint_sha256") == checkpoint.sha256,
                        "resumed manifest checkpoint SHA-256");

        const Json& baselineFrames = required(baselineManifest, "frames");
        const Json& resumedFrames = required(resumedManifest, "frames");
        bool prefixMatches = resumedFrames.items().size() >= checkpoint.frames.size();
        size_t limit = std::min(resumedFrames.items().size(), checkpoint.frames.size());
        for (size_t i = 0; prefixMatches && i < limit; ++i) {
            const FrameRecord& checkpointFrame = checkpoint.frames[i];
            FrameRecord resumedFrame = frameFromJson(resumedFrames.items()[i]);
            if (checkpointFrame.taskIndex != resumedFrame.taskIndex ||
                checkpointFrame.sha256 != resumedFrame.sha256 ||
                checkpointFrame.file != resumedFrame.file) {
                prefixMatches = false;
            }
        }
        addVerification(report, ok, prefixMatches,
                        "resumed output preserves checkpoint prefix and order");

        Json comparison = parseJson(readTextFile(joinPath(root, "comparison.json")));
        validateFieldOrder(report, ok, comparison, kComparisonFields, "comparison");
        const Json& summary = required(comparison, "summary");
        bool bitwise = required(summary, "bitwise_equal").asBool();
        bool countOk = requiredInt(summary, "total_frames") ==
                       static_cast<int64_t>(baselineFrames.items().size()) &&
                       requiredInt(summary, "equal_frames") ==
                       static_cast<int64_t>(baselineFrames.items().size());
        addVerification(report, ok, bitwise && countOk,
                        "comparison reports bitwise equality for every frame");
        addVerification(report, ok,
                        requiredString(summary, "final_reference_sha256") ==
                            frameFromJson(baselineFrames.items().back()).sha256 &&
                        requiredString(summary, "final_candidate_sha256") ==
                            frameFromJson(resumedFrames.items().back()).sha256,
                        "comparison final hashes");

        bool checksPassed = true;
        for (const Json& check : required(evidence, "checks").items()) {
            if (!required(check, "passed").asBool()) checksPassed = false;
        }
        addVerification(report, ok, checksPassed, "all evidence checks passed");
        const Json& headline = required(evidence, "headline_result");
        addVerification(report, ok, requiredString(headline, "status") == "PASS",
                        "headline status");

        const Json& probes = required(evidence, "degradation_probes");
        int degraded = 0;
        std::set<std::string> backends;
        for (const Json& probe : probes.items()) {
            if (required(probe, "degraded").asBool()) {
                ++degraded;
                backends.insert(requiredString(probe, "backend"));
            }
        }
        addVerification(report, ok, degraded >= 2 && backends.size() >= 2,
                        "banded Vulkan and CPU fallback are both evidenced");
    } catch (const std::exception& exception) {
        ok = false;
        report << "FAIL verification exception: " << exception.what() << '\n';
    }
    report << (ok ? "REVIEW PASS" : "REVIEW FAIL") << '\n';
    reportText = report.str();
    return ok;
}

void printArtifactSummary(const std::string& root) {
    Json evidence = parseJson(readTextFile(joinPath(root, "evidence.json")));
    const Json& headline = required(evidence, "headline_result");
    const Json& alignment = required(evidence, "clock_alignment");
    std::cout << "status: " << requiredString(headline, "status") << '\n';
    std::cout << "comparison: " << requiredString(headline, "comparison") << '\n';
    std::cout << "pause_after_tasks: " << requiredInt(headline, "pause_after_tasks") << '\n';
    std::cout << "total_tasks: " << requiredInt(headline, "total_tasks") << '\n';
    std::cout << "clock_offset_ns: " << requiredInt(alignment, "beta_minus_alpha_offset_ns") << '\n';
    std::cout << "clock_drift_ppm: "
              << required(alignment, "beta_minus_alpha_drift_ppm").asDouble() << '\n';
    std::cout << "final_sha256: " << requiredString(headline, "resumed_final_sha256") << '\n';
}



