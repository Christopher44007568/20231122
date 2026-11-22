#include "artifact.h"

#include "ordered_json.h"
#include "sha256.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ph {
namespace {

const std::vector<std::string> kRootRunOrder = {
    "schema", "schema_version", "run_id", "mode", "status", "runtime", "clock_model",
    "resource_plan", "workload", "devices", "pause_resume", "before_after",
    "continuous_comparison", "diagnostics", "field_order", "artifact_io",
    "artifact_digest",
};

const std::vector<std::string> kRootCheckpointOrder = {
    "schema", "schema_version", "run_id", "mode", "status", "runtime", "clock_model",
    "resource_plan", "workload", "progress", "devices", "checkpoint_evidence",
    "diagnostics", "field_order", "artifact_io", "artifact_digest",
};

const std::vector<std::string> kFieldOrderNames = {
    "root", "runtime", "clock_model", "resource_plan", "degradation", "device_stream",
    "frame", "probe", "summary", "pause_resume", "before_after", "continuous_comparison",
    "progress", "checkpoint_evidence", "diagnostics",
};

Json stringArray(const std::vector<std::string>& values) {
    Json array = Json::array();
    for (const std::string& value : values) {
        array.push(Json(value));
    }
    return array;
}

Json rgbaJson(const int rgba[4]) {
    Json array = Json::array();
    for (int i = 0; i < 4; ++i) {
        array.push(Json(rgba[i]));
    }
    return array;
}

Json clockJson(const ClockConfig& clock) {
    Json value = Json::object();
    value.set("frame_period_us", Json(clock.frame_period_us));
    value.set("frame_count", Json(clock.frame_count));
    value.set("device_a_epoch_us", Json(clock.device_a_epoch_us));
    value.set("device_b_epoch_us", Json(clock.device_b_epoch_us));
    value.set("device_b_initial_phase_us", Json(clock.device_b_initial_phase_us));
    value.set("skew_ppm", Json(clock.skew_ppm));
    value.set("object_period_us", Json(clock.object_period_us));
    value.set("object_half_extent", Json(static_cast<double>(clock.object_half_extent), 6));
    value.set("orbit_radius", Json(static_cast<double>(clock.orbit_radius), 6));
    value.set("probe_count", Json(clock.probe_count));
    Json formulas = Json::object();
    formulas.set("truth_tick_us", Json("step * frame_period_us"));
    formulas.set("device_a_raw_us", Json("truth_tick_us + device_a_epoch_us"));
    formulas.set("device_b_raw_us",
                 Json("device_b_epoch_us + round((truth_tick_us + "
                      "device_b_initial_phase_us) * (1 + skew_ppm / 1000000))"));
    formulas.set("device_a_scene_us", Json("device_a_raw_us - device_a_epoch_us"));
    formulas.set("device_b_scene_us", Json("device_b_raw_us - device_b_epoch_us"));
    formulas.set("device_b_calibrated_scene_us",
                 Json("round((device_b_raw_us - device_b_epoch_us) / (1 + skew_ppm / "
                      "1000000)) - device_b_initial_phase_us"));
    value.set("formulas", std::move(formulas));
    return value;
}

Json degradationJson(const DegradationStep& step) {
    Json value = Json::object();
    value.set("from_quality", Json(step.from_quality));
    value.set("to_quality", Json(step.to_quality));
    value.set("reason", Json(step.reason));
    value.set("estimated_bytes", Json(step.estimated_bytes));
    value.set("budget_bytes", Json(step.budget_bytes));
    value.set("effect", Json(step.effect));
    return value;
}

Json resourcePlanJson(const ResourcePlan& plan) {
    Json value = Json::object();
    value.set("requested_quality", Json(plan.requested_quality));
    value.set("max_quality", Json(plan.max_quality));
    value.set("effective_quality", Json(plan.effective_quality));
    value.set("status", Json(plan.status));
    value.set("reason", Json(plan.reason));
    value.set("width", Json(plan.width));
    value.set("height", Json(plan.height));
    value.set("retained_frames", Json(plan.retained_frames));
    value.set("budget_bytes", Json(plan.budget_bytes));
    value.set("estimated_bytes", Json(plan.estimated_bytes));
    Json steps = Json::array();
    for (const DegradationStep& step : plan.degradations) {
        steps.push(degradationJson(step));
    }
    value.set("degradations", std::move(steps));
    Json attempts = Json::array();
    for (const std::string& attempt : plan.allocation_attempts) {
        attempts.push(Json(attempt));
    }
    value.set("allocation_attempts", std::move(attempts));
    return value;
}

Json summaryJson(const RunSummary& summary) {
    Json value = Json::object();
    value.set("frame_records", Json(summary.frames));
    value.set("probe_samples", Json(summary.probe_samples));
    value.set("object_hits", Json(summary.object_hits));
    value.set("sample_digest", Json(summary.sample_digest));
    return value;
}

Json probeJson(const ProbeSample& sample) {
    Json value = Json::object();
    value.set("probe", Json(sample.probe));
    value.set("x", Json(sample.x));
    value.set("y", Json(sample.y));
    value.set("rgba", rgbaJson(sample.rgba));
    value.set("object_id", Json(sample.object_id));
    value.set("object_name", Json(sample.object_name));
    value.set("model_expected_rgba", rgbaJson(sample.model_rgba));
    return value;
}

Json frameJson(const FrameRecord& frame) {
    Json value = Json::object();
    value.set("step", Json(frame.step));
    value.set("device", Json(frame.device));
    value.set("raw_clock_us", Json(frame.raw_clock_us));
    value.set("truth_tick_us", Json(frame.truth_tick_us));
    value.set("scene_tick_us", Json(frame.scene_tick_us));
    value.set("calibrated_tick_us", Json(frame.calibrated_tick_us));
    value.set("clock_delta_vs_a_us", Json(frame.clock_delta_vs_a_us));
    value.set("frame_digest", Json(frame.frame_digest));
    Json probes = Json::array();
    for (const ProbeSample& sample : frame.probes) {
        probes.push(probeJson(sample));
    }
    value.set("probes", std::move(probes));
    return value;
}

Json deviceJson(const DeviceStream& stream) {
    Json value = Json::object();
    value.set("device", Json(stream.id));
    value.set("device_index", Json(stream.device_index));
    value.set("clock_epoch_us", Json(stream.epoch_us));
    Json frames = Json::array();
    for (const FrameRecord& frame : stream.frames) {
        frames.push(frameJson(frame));
    }
    value.set("frames", std::move(frames));
    return value;
}

Json runtimeJson(const RunResult& result) {
    Json value = Json::object();
    value.set("technology", Json("C++17"));
    value.set("graphics_api", Json("Vulkan"));
    value.set("window_backend", Json("GLFW"));
    value.set("vulkan_device", Json(result.runtime_device));
    value.set("vulkan_api_version", Json(result.runtime_api_version));
    value.set("render_mode", Json("offscreen color attachment + host readback"));
    value.set("readback_format", Json("R8G8B8A8_UNORM"));
    value.set("deterministic_scene", Json(true));
    return value;
}

Json workloadJson(const RunResult& result) {
    Json value = Json::object();
    value.set("frame_count", Json(result.frame_count));
    value.set("pause_after", Json(result.pause_after));
    value.set("next_step", Json(result.next_step));
    value.set("probe_count", Json(result.clock.probe_count));
    value.set("repeated_object_count", Json(4));
    value.set("scene", Json("four identical squares orbit on one circle; probes sit on that circle"));
    Json order = Json::array();
    order.push(Json("device_a"));
    order.push(Json("device_b"));
    value.set("device_order", std::move(order));
    return value;
}

Json fieldOrderJson(const std::vector<std::string>& root_order) {
    Json value = Json::object();
    value.set("root", stringArray(root_order));
    value.set("runtime", stringArray({"technology", "graphics_api", "window_backend",
                                      "vulkan_device", "vulkan_api_version", "render_mode",
                                      "readback_format", "deterministic_scene"}));
    value.set("clock_model", stringArray({"frame_period_us", "frame_count", "device_a_epoch_us",
                                          "device_b_epoch_us", "device_b_initial_phase_us",
                                          "skew_ppm", "object_period_us", "object_half_extent",
                                          "orbit_radius", "probe_count", "formulas"}));
    value.set("resource_plan", stringArray({"requested_quality", "max_quality",
                                            "effective_quality", "status", "reason", "width",
                                            "height", "retained_frames", "budget_bytes",
                                            "estimated_bytes", "degradations",
                                            "allocation_attempts"}));
    value.set("degradation", stringArray({"from_quality", "to_quality", "reason",
                                          "estimated_bytes", "budget_bytes", "effect"}));
    value.set("device_stream",
              stringArray({"device", "device_index", "clock_epoch_us", "frames"}));
    value.set("frame", stringArray({"step", "device", "raw_clock_us", "truth_tick_us",
                                    "scene_tick_us", "calibrated_tick_us",
                                    "clock_delta_vs_a_us", "frame_digest", "probes"}));
    value.set("probe", stringArray({"probe", "x", "y", "rgba", "object_id", "object_name",
                                    "model_expected_rgba"}));
    value.set("summary", stringArray({"frame_records", "probe_samples", "object_hits",
                                      "sample_digest"}));
    value.set("pause_resume", stringArray({"used", "resume_start_step", "checkpoint_step",
                                           "checkpoint_file", "resume_evidence"}));
    value.set("before_after", stringArray({"before_summary", "after_summary", "comparison"}));
    value.set("continuous_comparison",
              stringArray({"evaluated", "match", "reference_path", "reference_digest",
                           "current_digest"}));
    value.set("progress", stringArray({"next_step", "last_completed_step", "total_steps",
                                       "paused"}));
    value.set("checkpoint_evidence",
              stringArray({"last_completed_step", "next_step", "device_a", "device_b",
                           "combined_sample_digest", "resume_contract"}));
    value.set("diagnostics", stringArray({"notes"}));
    return value;
}

Json diagnosticsJson(const RunResult& result) {
    Json value = Json::object();
    Json notes = Json::array();
    for (const std::string& note : result.notes) {
        notes.push(Json(note));
    }
    value.set("notes", std::move(notes));
    return value;
}

Json resumeEvidenceJson(const ResumeEvidence& evidence) {
    Json value = Json::object();
    value.set("loaded", Json(evidence.loaded));
    value.set("artifact_digest_valid", Json(evidence.artifact_digest_valid));
    value.set("checkpoint_roundtrip_byte_stable",
              Json(evidence.checkpoint_roundtrip_byte_stable));
    value.set("checkpoint_field_order_preserved",
              Json(evidence.checkpoint_field_order_preserved));
    value.set("checkpoint_step", Json(evidence.checkpoint_step));
    value.set("compared_pixels", Json(evidence.compared_pixels));
    value.set("mismatches", Json(evidence.mismatches));
    value.set("frame_digest_match", Json(evidence.frame_digest_match));
    value.set("match", Json(evidence.match));
    value.set("checkpoint_digest", Json(evidence.checkpoint_digest));
    value.set("rerender_digest_device_a", Json(evidence.rerender_digest_device_a));
    value.set("rerender_digest_device_b", Json(evidence.rerender_digest_device_b));
    return value;
}

Json pauseResumeJson(const RunResult& result) {
    Json value = Json::object();
    value.set("used", Json(result.resume.loaded));
    value.set("resume_start_step", Json(result.resume_start_step));
    value.set("checkpoint_step", Json(result.resume.checkpoint_step));
    value.set("checkpoint_file", Json(result.checkpoint_path));
    value.set("resume_evidence", resumeEvidenceJson(result.resume));
    return value;
}

Json continuousComparisonJson(const ContinuousComparison& comparison) {
    Json value = Json::object();
    value.set("evaluated", Json(comparison.evaluated));
    value.set("match", Json(comparison.match));
    value.set("reference_path", Json(comparison.reference_path));
    value.set("reference_digest", Json(comparison.reference_digest));
    value.set("current_digest", Json(comparison.current_digest));
    return value;
}

Json beforeAfterJson(const RunResult& result) {
    Json value = Json::object();
    value.set("before_summary", summaryJson(result.pre_summary));
    value.set("after_summary", summaryJson(result.post_summary));
    Json comparison = Json::object();
    comparison.set("checkpoint_step", Json(result.resume.checkpoint_step));
    comparison.set("resume_start_step", Json(result.resume_start_step));
    comparison.set("resume_rerender_match", Json(result.resume.match));
    comparison.set("resume_mismatched_pixels", Json(result.resume.mismatches));
    comparison.set("continuous_run_match", Json(result.comparison.match));
    value.set("comparison", std::move(comparison));
    return value;
}

Json artifactIoJson() {
    Json value = Json::object();
    value.set("encoding", Json("UTF-8"));
    value.set("pretty_indent", Json(2));
    value.set("roundtrip_byte_stable", Json(false));
    value.set("field_order_preserved", Json(false));
    value.set("hash_algorithm", Json("SHA-256"));
    value.set("digest_scope", Json("compact JSON with artifact_digest removed"));
    return value;
}

const FrameRecord* lastFrame(const DeviceStream& stream) {
    return stream.frames.empty() ? nullptr : &stream.frames.back();
}

Json checkpointDeviceEvidenceJson(const DeviceStream& stream) {
    Json value = Json::object();
    const FrameRecord* frame = lastFrame(stream);
    value.set("frame_digest", Json(frame == nullptr ? "" : frame->frame_digest));
    value.set("last_frame", frame == nullptr ? Json() : frameJson(*frame));
    return value;
}

Json checkpointEvidenceJson(const RunResult& result) {
    Json value = Json::object();
    const int last_step = result.next_step - 1;
    value.set("last_completed_step", Json(last_step));
    value.set("next_step", Json(result.next_step));
    value.set("device_a", checkpointDeviceEvidenceJson(result.devices[0]));
    value.set("device_b", checkpointDeviceEvidenceJson(result.devices[1]));
    value.set("combined_sample_digest", Json(streamDigest(result)));
    Json contract = Json::object();
    contract.set("checkpoint_schema", Json("pixel-history.checkpoint"));
    contract.set("frame_digest_algorithm", Json("SHA-256"));
    contract.set("pixel_comparison", Json("compare every stored probe RGBA and object_id"));
    contract.set("clock_fields",
                 Json("raw_clock_us, truth_tick_us, scene_tick_us, calibrated_tick_us"));
    contract.set("continue_from_next_step", Json(result.next_step));
    value.set("resume_contract", std::move(contract));
    return value;
}

Json progressJson(const RunResult& result) {
    Json value = Json::object();
    value.set("next_step", Json(result.next_step));
    value.set("last_completed_step", Json(result.next_step - 1));
    value.set("total_steps", Json(result.frame_count));
    value.set("paused", Json(result.paused));
    return value;
}

void setDigest(Json& root) {
    root.erase("artifact_digest");
    Json digest = Json::object();
    digest.set("algorithm", Json("SHA-256"));
    digest.set("scope", Json("compact JSON with artifact_digest removed"));
    digest.set("value", Json(sha256Hex(root.dumpCompact())));
    root.set("artifact_digest", std::move(digest));
}

bool keysMatchOrder(const Json& object, const Json& order, std::string& error) {
    if (!object.isObject() || !order.isArray()) {
        error = "field_order entry is not an array";
        return false;
    }
    const auto keys = object.keys();
    if (keys.size() != order.size()) {
        error = "field count does not match field_order";
        return false;
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        if (!order.items()[i].isString() || keys[i] != order.items()[i].asString()) {
            error = "field order mismatch at position " + std::to_string(i) + ": " + keys[i];
            return false;
        }
    }
    return true;
}

std::string finalizeArtifact(Json& root, std::string& error) {
    root.set("artifact_digest", Json::object());
    const std::string first_pretty = root.dumpPretty(2);
    Json parsed;
    std::string parse_error;
    if (!Json::parse(first_pretty, parsed, parse_error)) {
        error = "self round-trip parse failed: " + parse_error;
        return {};
    }
    const bool roundtrip = parsed.dumpPretty(2) == first_pretty;
    bool order = false;
    const Json* field_order = root.find("field_order");
    const Json* root_order = field_order == nullptr ? nullptr : field_order->find("root");
    if (root_order != nullptr) {
        order = keysMatchOrder(root, *root_order, error);
        if (!order) {
            return {};
        }
    }
    Json* io = root.find("artifact_io");
    if (io != nullptr) {
        io->set("roundtrip_byte_stable", Json(roundtrip));
        io->set("field_order_preserved", Json(order));
    }
    setDigest(root);
    return root.dumpPretty(2);
}

Json runRoot(const RunResult& result) {
    Json root = Json::object();
    root.set("schema", Json(result.schema));
    root.set("schema_version", Json(result.schema_version));
    root.set("run_id", Json(result.run_id));
    root.set("mode", Json(result.mode));
    root.set("status", Json(result.status));
    root.set("runtime", runtimeJson(result));
    root.set("clock_model", clockJson(result.clock));
    root.set("resource_plan", resourcePlanJson(result.plan));
    root.set("workload", workloadJson(result));
    Json devices = Json::array();
    for (const DeviceStream& stream : result.devices) {
        devices.push(deviceJson(stream));
    }
    root.set("devices", std::move(devices));
    root.set("pause_resume", pauseResumeJson(result));
    root.set("before_after", beforeAfterJson(result));
    root.set("continuous_comparison", continuousComparisonJson(result.comparison));
    root.set("diagnostics", diagnosticsJson(result));
    root.set("field_order", fieldOrderJson(kRootRunOrder));
    root.set("artifact_io", artifactIoJson());
    return root;
}

Json checkpointRoot(const RunResult& result) {
    Json root = Json::object();
    root.set("schema", Json("pixel-history.checkpoint"));
    root.set("schema_version", Json(result.schema_version));
    root.set("run_id", Json(result.run_id));
    root.set("mode", Json(result.mode));
    root.set("status", Json(result.status));
    root.set("runtime", runtimeJson(result));
    root.set("clock_model", clockJson(result.clock));
    root.set("resource_plan", resourcePlanJson(result.plan));
    root.set("workload", workloadJson(result));
    root.set("progress", progressJson(result));
    Json devices = Json::array();
    for (const DeviceStream& stream : result.devices) {
        devices.push(deviceJson(stream));
    }
    root.set("devices", std::move(devices));
    root.set("checkpoint_evidence", checkpointEvidenceJson(result));
    root.set("diagnostics", diagnosticsJson(result));
    root.set("field_order", fieldOrderJson(kRootCheckpointOrder));
    root.set("artifact_io", artifactIoJson());
    return root;
}

bool writeText(const std::string& path, const std::string& text, std::string& error) {
    try {
        const std::filesystem::path target(path);
        if (target.has_parent_path()) {
            std::filesystem::create_directories(target.parent_path());
        }
        std::ofstream stream(target, std::ios::binary | std::ios::trunc);
        if (!stream) {
            error = "cannot open output file: " + path;
            return false;
        }
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!stream) {
            error = "cannot write output file: " + path;
            return false;
        }
    } catch (const std::exception& exception) {
        error = std::string("filesystem error: ") + exception.what();
        return false;
    }
    return true;
}

const std::vector<std::string> kFailureRootOrder = {
    "schema", "schema_version", "status", "reason", "technology", "resource_plan",
    "degradation_path", "recommended_action", "field_order", "artifact_io",
    "artifact_digest",
};

Json failureRoot(const AppConfig&, const ResourcePlan& plan) {
    Json root = Json::object();
    root.set("schema", Json("pixel-history.failure"));
    root.set("schema_version", Json(1));
    root.set("status", Json("resource_blocked"));
    root.set("reason", Json(plan.reason));
    root.set("technology", Json("C++17 + Vulkan + GLFW"));
    root.set("resource_plan", resourcePlanJson(plan));
    Json path = Json::array();
    for (const DegradationStep& step : plan.degradations) {
        path.push(degradationJson(step));
    }
    root.set("degradation_path", std::move(path));
    root.set("recommended_action",
             Json("increase --memory-budget-kb or lower --max-quality explicitly"));
    Json order = Json::object();
    order.set("root", stringArray(kFailureRootOrder));
    root.set("field_order", std::move(order));
    root.set("artifact_io", artifactIoJson());
    return root;
}

bool loadJsonFile(const std::string& path, std::string& raw, Json& root, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open artifact: " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    raw = buffer.str();
    if (raw.empty()) {
        error = "artifact is empty: " + path;
        return false;
    }
    if (!Json::parse(raw, root, error)) {
        error = "invalid artifact JSON: " + error;
        return false;
    }
    return true;
}

bool verifyDigest(const Json& root, std::string& value, std::string& error) {
    const Json* digest = root.find("artifact_digest");
    if (digest == nullptr || !digest->isObject()) {
        error = "artifact_digest object is missing";
        return false;
    }
    const Json* algorithm = digest->find("algorithm");
    const Json* stored = digest->find("value");
    if (algorithm == nullptr || !algorithm->isString() || algorithm->asString() != "SHA-256" ||
        stored == nullptr || !stored->isString()) {
        error = "artifact_digest fields are invalid";
        return false;
    }
    Json copy = root;
    copy.erase("artifact_digest");
    const std::string actual = sha256Hex(copy.dumpCompact());
    if (actual != stored->asString()) {
        error = "artifact_digest mismatch";
        return false;
    }
    value = actual;
    return true;
}

bool checkDeclaredOrder(const Json& root, const std::string& field, std::string& error) {
    const Json* field_order = root.find("field_order");
    if (field_order == nullptr) {
        error = "field_order is missing";
        return false;
    }
    const Json* order = field_order->find(field);
    if (order == nullptr) {
        error = "field_order." + field + " is missing";
        return false;
    }
    if (!keysMatchOrder(root, *order, error)) {
        error = "root " + error;
        return false;
    }
    return true;
}

bool checkNestedOrder(const Json& object, const Json& field_order, const std::string& name,
                      std::string& error) {
    const Json* order = field_order.find(name);
    if (order == nullptr) {
        error = "field_order." + name + " is missing";
        return false;
    }
    if (!keysMatchOrder(object, *order, error)) {
        error = name + ": " + error;
        return false;
    }
    return true;
}

bool readString(const Json& object, const char* key, std::string& out, std::string& error) {
    const Json* value = object.find(key);
    if (value == nullptr || !value->isString()) {
        error = std::string("missing string field ") + key;
        return false;
    }
    out = value->asString();
    return true;
}

bool readInt(const Json& object, const char* key, int64_t& out, std::string& error) {
    const Json* value = object.find(key);
    if (value == nullptr || !value->isNumber()) {
        error = std::string("missing number field ") + key;
        return false;
    }
    try {
        out = value->asInt();
    } catch (const std::exception& exception) {
        error = std::string("invalid integer field ") + key + ": " + exception.what();
        return false;
    }
    return true;
}

bool readBool(const Json& object, const char* key, bool& out, std::string& error) {
    const Json* value = object.find(key);
    if (value == nullptr || !value->isBool()) {
        error = std::string("missing boolean field ") + key;
        return false;
    }
    out = value->asBool();
    return true;
}

bool parseClock(const Json& value, ClockConfig& clock, std::string& error) {
    int64_t number = 0;
    if (!readInt(value, "frame_period_us", number, error)) return false;
    clock.frame_period_us = number;
    if (!readInt(value, "frame_count", number, error)) return false;
    clock.frame_count = static_cast<int>(number);
    if (!readInt(value, "device_a_epoch_us", number, error)) return false;
    clock.device_a_epoch_us = number;
    if (!readInt(value, "device_b_epoch_us", number, error)) return false;
    clock.device_b_epoch_us = number;
    if (!readInt(value, "device_b_initial_phase_us", number, error)) return false;
    clock.device_b_initial_phase_us = number;
    if (!readInt(value, "skew_ppm", number, error)) return false;
    clock.skew_ppm = static_cast<int>(number);
    if (!readInt(value, "object_period_us", number, error)) return false;
    clock.object_period_us = number;
    const Json* half = value.find("object_half_extent");
    const Json* radius = value.find("orbit_radius");
    if (half == nullptr || !half->isNumber() || radius == nullptr || !radius->isNumber()) {
        error = "clock scene geometry fields are invalid";
        return false;
    }
    clock.object_half_extent = static_cast<float>(half->asDouble());
    clock.orbit_radius = static_cast<float>(radius->asDouble());
    if (!readInt(value, "probe_count", number, error)) return false;
    clock.probe_count = static_cast<int>(number);
    return true;
}

bool parsePlan(const Json& value, ResourcePlan& plan, std::string& error) {
    if (!readString(value, "requested_quality", plan.requested_quality, error) ||
        !readString(value, "max_quality", plan.max_quality, error) ||
        !readString(value, "effective_quality", plan.effective_quality, error) ||
        !readString(value, "status", plan.status, error) ||
        !readString(value, "reason", plan.reason, error)) {
        return false;
    }
    int64_t number = 0;
    if (!readInt(value, "width", number, error)) return false;
    plan.width = static_cast<int>(number);
    if (!readInt(value, "height", number, error)) return false;
    plan.height = static_cast<int>(number);
    if (!readInt(value, "retained_frames", number, error)) return false;
    plan.retained_frames = static_cast<int>(number);
    if (!readInt(value, "budget_bytes", number, error)) return false;
    plan.budget_bytes = number;
    if (!readInt(value, "estimated_bytes", number, error)) return false;
    plan.estimated_bytes = number;
    const Json* degradations = value.find("degradations");
    if (degradations == nullptr || !degradations->isArray()) {
        error = "resource_plan.degradations is invalid";
        return false;
    }
    for (const Json& item : degradations->items()) {
        DegradationStep step;
        if (!readString(item, "from_quality", step.from_quality, error) ||
            !readString(item, "to_quality", step.to_quality, error) ||
            !readString(item, "reason", step.reason, error) ||
            !readInt(item, "estimated_bytes", step.estimated_bytes, error) ||
            !readInt(item, "budget_bytes", step.budget_bytes, error) ||
            !readString(item, "effect", step.effect, error)) {
            return false;
        }
        plan.degradations.push_back(std::move(step));
    }
    const Json* attempts = value.find("allocation_attempts");
    if (attempts == nullptr || !attempts->isArray()) {
        error = "resource_plan.allocation_attempts is invalid";
        return false;
    }
    for (const Json& item : attempts->items()) {
        if (!item.isString()) {
            error = "allocation_attempts contains a non-string";
            return false;
        }
        plan.allocation_attempts.push_back(item.asString());
    }
    return true;
}

bool readRgba(const Json& value, int rgba[4], std::string& error) {
    if (!value.isArray() || value.size() != 4) {
        error = "rgba must be a four-element array";
        return false;
    }
    for (size_t i = 0; i < 4; ++i) {
        if (!value.items()[i].isNumber()) {
            error = "rgba contains a non-number";
            return false;
        }
        rgba[i] = static_cast<int>(value.items()[i].asInt());
    }
    return true;
}

bool parseProbe(const Json& value, ProbeSample& sample, std::string& error) {
    if (!readString(value, "probe", sample.probe, error) ||
        !readString(value, "object_name", sample.object_name, error)) {
        return false;
    }
    int64_t number = 0;
    if (!readInt(value, "x", number, error)) return false;
    sample.x = static_cast<int>(number);
    if (!readInt(value, "y", number, error)) return false;
    sample.y = static_cast<int>(number);
    if (!readInt(value, "object_id", number, error)) return false;
    sample.object_id = static_cast<int>(number);
    const Json* rgba = value.find("rgba");
    const Json* expected = value.find("model_expected_rgba");
    if (rgba == nullptr || expected == nullptr || !readRgba(*rgba, sample.rgba, error) ||
        !readRgba(*expected, sample.model_rgba, error)) {
        return false;
    }
    return true;
}

bool parseFrame(const Json& value, const std::string& device, FrameRecord& frame,
                std::string& error) {
    int64_t number = 0;
    if (!readInt(value, "step", number, error)) return false;
    frame.step = static_cast<int>(number);
    if (!readString(value, "device", frame.device, error) ||
        !readString(value, "frame_digest", frame.frame_digest, error)) {
        return false;
    }
    if (frame.device != device) {
        error = "frame device does not match its device stream";
        return false;
    }
    if (!readInt(value, "raw_clock_us", frame.raw_clock_us, error) ||
        !readInt(value, "truth_tick_us", frame.truth_tick_us, error) ||
        !readInt(value, "scene_tick_us", frame.scene_tick_us, error) ||
        !readInt(value, "calibrated_tick_us", frame.calibrated_tick_us, error) ||
        !readInt(value, "clock_delta_vs_a_us", frame.clock_delta_vs_a_us, error)) {
        return false;
    }
    const Json* probes = value.find("probes");
    if (probes == nullptr || !probes->isArray()) {
        error = "frame.probes is invalid";
        return false;
    }
    for (const Json& item : probes->items()) {
        ProbeSample sample;
        if (!parseProbe(item, sample, error)) {
            return false;
        }
        frame.probes.push_back(std::move(sample));
    }
    if (frameDigest(frame) != frame.frame_digest) {
        error = "frame digest mismatch at step " + std::to_string(frame.step);
        return false;
    }
    return true;
}

bool parseDevices(const Json& value, std::vector<DeviceStream>& devices, std::string& error) {
    if (!value.isArray() || value.size() != 2) {
        error = "devices must contain exactly device_a and device_b";
        return false;
    }
    devices.clear();
    for (size_t index = 0; index < 2; ++index) {
        const Json& item = value.items()[index];
        DeviceStream stream;
        int64_t number = 0;
        if (!readString(item, "device", stream.id, error) ||
            !readInt(item, "device_index", number, error)) {
            return false;
        }
        stream.device_index = static_cast<int>(number);
        if (!readInt(item, "clock_epoch_us", stream.epoch_us, error)) {
            return false;
        }
        const std::string expected = index == 0 ? "device_a" : "device_b";
        if (stream.id != expected || stream.device_index != static_cast<int>(index)) {
            error = "device stream order/identity is not device_a,device_b";
            return false;
        }
        const Json* frames = item.find("frames");
        if (frames == nullptr || !frames->isArray()) {
            error = "device stream frames is invalid";
            return false;
        }
        for (const Json& frame_value : frames->items()) {
            FrameRecord frame;
            if (!parseFrame(frame_value, stream.id, frame, error)) {
                return false;
            }
            stream.frames.push_back(std::move(frame));
        }
        devices.push_back(std::move(stream));
    }
    return true;
}

bool parseRunData(const Json& root, RunResult& result, std::string& error) {
    int64_t number = 0;
    if (!readString(root, "run_id", result.run_id, error) ||
        !readString(root, "mode", result.mode, error) ||
        !readString(root, "status", result.status, error)) {
        return false;
    }
    if (!readInt(root, "schema_version", number, error)) return false;
    result.schema_version = static_cast<int>(number);
    const Json* runtime = root.find("runtime");
    const Json* clock = root.find("clock_model");
    const Json* plan = root.find("resource_plan");
    const Json* workload = root.find("workload");
    const Json* devices = root.find("devices");
    if (runtime == nullptr || clock == nullptr || plan == nullptr || workload == nullptr ||
        devices == nullptr) {
        error = "artifact is missing a required top-level section";
        return false;
    }
    if (!readString(*runtime, "vulkan_device", result.runtime_device, error) ||
        !readString(*runtime, "vulkan_api_version", result.runtime_api_version, error) ||
        !parseClock(*clock, result.clock, error) || !parsePlan(*plan, result.plan, error)) {
        return false;
    }
    if (!readInt(*workload, "frame_count", number, error)) return false;
    result.frame_count = static_cast<int>(number);
    if (!readInt(*workload, "pause_after", number, error)) return false;
    result.pause_after = static_cast<int>(number);
    if (!readInt(*workload, "next_step", number, error)) return false;
    result.next_step = static_cast<int>(number);
    if (!parseDevices(*devices, result.devices, error)) {
        return false;
    }
    if (result.clock.frame_count != result.frame_count) {
        error = "clock frame_count disagrees with workload frame_count";
        return false;
    }
    for (const DeviceStream& stream : result.devices) {
        if (static_cast<int>(stream.frames.size()) != result.next_step) {
            error = "device stream frame count disagrees with next_step";
            return false;
        }
        for (size_t i = 0; i < stream.frames.size(); ++i) {
            if (stream.frames[i].step != static_cast<int>(i)) {
                error = "device frames are not in ascending step order";
                return false;
            }
        }
    }
    const Json* pause = root.find("pause_resume");
    if (pause != nullptr) {
        bool used = false;
        if (!readBool(*pause, "used", used, error) ||
            !readInt(*pause, "resume_start_step", number, error)) {
            return false;
        }
        result.resume.loaded = used;
        result.resume_start_step = static_cast<int>(number);
        if (!readString(*pause, "checkpoint_file", result.checkpoint_path, error)) {
            return false;
        }
    }
    const Json* diagnostics = root.find("diagnostics");
    if (diagnostics != nullptr) {
        const Json* notes = diagnostics->find("notes");
        if (notes == nullptr || !notes->isArray()) {
            error = "diagnostics.notes is invalid";
            return false;
        }
        for (const Json& note : notes->items()) {
            if (!note.isString()) {
                error = "diagnostics.notes contains a non-string";
                return false;
            }
            result.notes.push_back(note.asString());
        }
    }
    return true;
}

bool validateNestedOrders(const Json& root, std::string& error) {
    const Json* field_order = root.find("field_order");
    const Json* clock = root.find("clock_model");
    const Json* plan = root.find("resource_plan");
    const Json* devices = root.find("devices");
    if (field_order == nullptr || clock == nullptr || plan == nullptr || devices == nullptr) {
        error = "artifact does not declare nested field order";
        return false;
    }
    if (!checkNestedOrder(*clock, *field_order, "clock_model", error) ||
        !checkNestedOrder(*plan, *field_order, "resource_plan", error)) {
        return false;
    }
    const Json* diagnostics = root.find("diagnostics");
    if (diagnostics != nullptr &&
        !checkNestedOrder(*diagnostics, *field_order, "diagnostics", error)) {
        return false;
    }
    for (const Json& device : devices->items()) {
        if (!checkNestedOrder(device, *field_order, "device_stream", error)) {
            return false;
        }
        const Json* frames = device.find("frames");
        if (frames == nullptr) return false;
        for (const Json& frame : frames->items()) {
            if (!checkNestedOrder(frame, *field_order, "frame", error)) {
                return false;
            }
            const Json* probes = frame.find("probes");
            if (probes == nullptr) return false;
            for (const Json& probe : probes->items()) {
                if (!checkNestedOrder(probe, *field_order, "probe", error)) {
                    return false;
                }
            }
        }
    }
    const Json* degradations = plan->find("degradations");
    if (degradations != nullptr) {
        for (const Json& step : degradations->items()) {
            if (!checkNestedOrder(step, *field_order, "degradation", error)) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

bool writeRunArtifact(const std::string& path, const RunResult& result, std::string& error) {
    Json root = runRoot(result);
    const std::string text = finalizeArtifact(root, error);
    if (text.empty()) {
        return false;
    }
    return writeText(path, text, error);
}

bool writeCheckpointArtifact(const std::string& path, const RunResult& result,
                             std::string& error) {
    Json root = checkpointRoot(result);
    const std::string text = finalizeArtifact(root, error);
    if (text.empty()) {
        return false;
    }
    return writeText(path, text, error);
}

bool writeBlockedArtifact(const std::string& path, const AppConfig& config,
                          const ResourcePlan& plan, std::string& error) {
    (void)config;
    Json root = failureRoot(config, plan);
    const std::string text = finalizeArtifact(root, error);
    if (text.empty()) {
        return false;
    }
    return writeText(path, text, error);
}

bool loadCheckpointArtifact(const std::string& path, LoadedCheckpoint& loaded,
                            std::string& error) {
    Json root;
    if (!loadJsonFile(path, loaded.raw_text, root, error)) {
        return false;
    }
    const Json* schema = root.find("schema");
    if (schema == nullptr || !schema->isString() ||
        schema->asString() != "pixel-history.checkpoint") {
        error = "artifact is not a pixel-history checkpoint";
        return false;
    }
    if (root.dumpPretty(2) != loaded.raw_text) {
        error = "checkpoint text is not in canonical field order/encoding";
        return false;
    }
    loaded.roundtrip_byte_stable = true;
    if (!checkDeclaredOrder(root, "root", error)) {
        return false;
    }
    loaded.field_order_preserved = true;
    if (!validateNestedOrders(root, error)) {
        return false;
    }
    if (!verifyDigest(root, loaded.artifact_digest, error)) {
        return false;
    }
    loaded.digest_valid = true;

    if (!parseRunData(root, loaded.prefix, error)) {
        return false;
    }
    const Json* progress = root.find("progress");
    if (progress == nullptr) {
        error = "checkpoint progress is missing";
        return false;
    }
    int64_t next_step = 0;
    if (!readInt(*progress, "next_step", next_step, error)) {
        return false;
    }
    loaded.next_step = static_cast<int>(next_step);
    if (loaded.prefix.next_step != loaded.next_step) {
        error = "checkpoint progress disagrees with workload next_step";
        return false;
    }
    loaded.prefix.resume_start_step = loaded.next_step;
    loaded.prefix.checkpoint_path = path;
    loaded.prefix.pre_summary = summarize(loaded.prefix, 0, -1);

    const Json* evidence = root.find("checkpoint_evidence");
    if (evidence == nullptr) {
        error = "checkpoint_evidence is missing";
        return false;
    }
    std::string combined;
    if (!readString(*evidence, "combined_sample_digest", combined, error)) {
        return false;
    }
    if (combined != streamDigest(loaded.prefix)) {
        error = "checkpoint combined sample digest mismatch";
        return false;
    }
    for (const char* device : {"device_a", "device_b"}) {
        const Json* item = evidence->find(device);
        if (item == nullptr) {
            error = std::string("checkpoint evidence is missing ") + device;
            return false;
        }
        std::string digest;
        if (!readString(*item, "frame_digest", digest, error)) {
            return false;
        }
        const DeviceStream& stream =
            std::string(device) == "device_a" ? loaded.prefix.devices[0]
                                              : loaded.prefix.devices[1];
        if (stream.frames.empty() || stream.frames.back().frame_digest != digest) {
            error = std::string("checkpoint last-frame evidence mismatch for ") + device;
            return false;
        }
    }
    return true;
}

bool verifyArtifact(const std::string& path, std::string& report_json, std::string& error) {
    std::string raw;
    Json root;
    if (!loadJsonFile(path, raw, root, error)) {
        return false;
    }
    Json checks = Json::array();
    bool all_ok = true;
    const auto add = [&](const std::string& name, bool ok, const std::string& detail) {
        Json check = Json::object();
        check.set("check", Json(name));
        check.set("ok", Json(ok));
        check.set("detail", Json(detail));
        checks.push(std::move(check));
        if (!ok) {
            all_ok = false;
        }
    };

    std::string local_error;
    const bool roundtrip = root.dumpPretty(2) == raw;
    add("canonical_roundtrip_and_field_order", roundtrip,
        roundtrip ? "parse + dump is byte-identical" : "JSON is not canonical");
    if (!roundtrip) {
        local_error = "non-canonical JSON";
    }
    const bool order_ok =
        roundtrip && checkDeclaredOrder(root, "root", local_error);
    add("root_field_order", order_ok, order_ok ? "root order matches declaration" : local_error);

    const Json* schema_value = root.find("schema");
    const std::string schema =
        schema_value != nullptr && schema_value->isString() ? schema_value->asString() : "";
    const bool nested_order_ok =
        schema != "pixel-history.failure" && order_ok && validateNestedOrders(root, local_error);
    add("nested_field_order", schema == "pixel-history.failure" ? true : nested_order_ok,
        schema == "pixel-history.failure"
            ? "not applicable to failure evidence"
            : (nested_order_ok ? "nested order matches declaration" : local_error));

    std::string digest;
    const bool digest_ok = verifyDigest(root, digest, local_error);
    add("artifact_sha256", digest_ok,
        digest_ok ? digest : local_error);

    if (schema == "pixel-history.failure") {
        const Json* status = root.find("status");
        const bool blocked = status != nullptr && status->isString() &&
                             status->asString() == "resource_blocked";
        add("resource_blocked_failure", blocked,
            blocked ? "failure evidence is complete" : "failure status missing");
    } else if (schema == "pixel-history.checkpoint") {
        LoadedCheckpoint loaded;
        if (!loadCheckpointArtifact(path, loaded, local_error)) {
            add("checkpoint_reload", false, local_error);
        } else {
            add("checkpoint_reload", true, "digest, order and frame evidence validated");
        }
    } else if (schema == "pixel-history.run") {
        RunResult result;
        const bool parsed = parseRunData(root, result, local_error);
        add("run_structure", parsed, parsed ? "all device/frame fields parsed" : local_error);
        if (parsed) {
            int model_errors = 0;
            int clock_errors = 0;
            std::string first_model_error;
            std::string first_clock_error;
            for (const DeviceStream& stream : result.devices) {
                for (const FrameRecord& frame : stream.frames) {
                    const ClockStep expected = clockStep(result.clock, frame.step);
                    const int64_t expected_raw = stream.device_index == 0
                                                     ? expected.device_a_raw_us
                                                     : expected.device_b_raw_us;
                    const int64_t expected_scene = stream.device_index == 0
                                                       ? expected.device_a_scene_us
                                                       : expected.device_b_scene_us;
                    const int64_t expected_calibrated =
                        stream.device_index == 0 ? expected.device_a_scene_us
                                                 : expected.device_b_calibrated_scene_us;
                    const int64_t expected_delta =
                        stream.device_index == 0 ? 0 : expected.device_b_minus_a_us;
                    if (frame.raw_clock_us != expected_raw ||
                        frame.truth_tick_us != expected.truth_tick_us ||
                        frame.scene_tick_us != expected_scene ||
                        frame.calibrated_tick_us != expected_calibrated ||
                        frame.clock_delta_vs_a_us != expected_delta) {
                        ++clock_errors;
                        if (first_clock_error.empty()) {
                            first_clock_error = "step " + std::to_string(frame.step) +
                                                " device " + frame.device;
                        }
                    }
                    for (const ProbeSample& probe : frame.probes) {
                        const PixelValue expected_pixel =
                            evaluatePixel(result.clock, probe.x, probe.y, result.plan.width,
                                          result.plan.height, frame.scene_tick_us);
                        if (probe.rgba[0] != expected_pixel.r || probe.rgba[1] != expected_pixel.g ||
                            probe.rgba[2] != expected_pixel.b || probe.rgba[3] != expected_pixel.a ||
                            probe.model_rgba[0] != probe.rgba[0] ||
                            probe.model_rgba[1] != probe.rgba[1] ||
                            probe.model_rgba[2] != probe.rgba[2] ||
                            probe.model_rgba[3] != probe.rgba[3] ||
                            probe.object_id != expected_pixel.object_id ||
                            probe.object_name != objectName(expected_pixel.object_id)) {
                            ++model_errors;
                            if (first_model_error.empty()) {
                                first_model_error = "step " + std::to_string(frame.step) +
                                                    " probe " + probe.probe;
                            }
                        }
                    }
                }
            }
            add("clock_formula_recompute", clock_errors == 0,
                clock_errors == 0 ? "all raw/scene/calibrated ticks match formulas"
                                  : std::to_string(clock_errors) + " mismatch(es), first " +
                                        first_clock_error);
            add("pixel_model_recompute", model_errors == 0,
                model_errors == 0 ? "all RGBA/object ids match the review model"
                                  : std::to_string(model_errors) + " mismatch(es), first " +
                                        first_model_error);

            const Json* before_after = root.find("before_after");
            bool summary_ok = before_after != nullptr;
            std::string summary_error = "before_after is missing";
            if (summary_ok) {
                const Json* before = before_after->find("before_summary");
                const Json* after = before_after->find("after_summary");
                const Json* before_digest =
                    before == nullptr ? nullptr : before->find("sample_digest");
                const Json* after_digest =
                    after == nullptr ? nullptr : after->find("sample_digest");
                summary_ok = before_digest != nullptr && after_digest != nullptr &&
                             before_digest->isString() && after_digest->isString();
                if (summary_ok) {
                    const int split = result.resume_start_step >= 0
                                          ? result.resume_start_step
                                          : result.frame_count;
                    const RunSummary computed_before = summarize(result, 0, split);
                    const RunSummary computed_after =
                        summarize(result, result.resume_start_step >= 0 ? split : 0, -1);
                    summary_ok = before_digest->asString() == computed_before.sample_digest &&
                                 after_digest->asString() == computed_after.sample_digest;
                    if (!summary_ok) {
                        summary_error = "before/after summary digests do not match device data";
                    }
                } else {
                    summary_error = "summary digests are missing";
                }
            }
            add("before_after_summary", summary_ok,
                summary_ok ? "stored summaries recompute from frame evidence" : summary_error);

            const Json* pause = root.find("pause_resume");
            bool resume_ok = false;
            std::string resume_detail = "no checkpoint was used";
            if (pause != nullptr) {
                const Json* used = pause->find("used");
                const Json* evidence = pause->find("resume_evidence");
                const bool uses_checkpoint = used != nullptr && used->isBool() && used->asBool();
                if (!uses_checkpoint) {
                    resume_ok = true;
                } else if (evidence != nullptr) {
                    const Json* match = evidence->find("match");
                    const Json* mismatches = evidence->find("mismatches");
                    const Json* checkpoint_step = evidence->find("checkpoint_step");
                    const Json* compared_pixels = evidence->find("compared_pixels");
                    const Json* checkpoint_digest = evidence->find("checkpoint_digest");
                    const Json* rerender_a = evidence->find("rerender_digest_device_a");
                    const Json* rerender_b = evidence->find("rerender_digest_device_b");
                    const Json* checkpoint_file = pause->find("checkpoint_file");
                    resume_ok = match != nullptr && match->isBool() && match->asBool() &&
                                mismatches != nullptr && mismatches->isNumber() &&
                                mismatches->asInt() == 0 && checkpoint_step != nullptr &&
                                checkpoint_step->isNumber() && compared_pixels != nullptr &&
                                compared_pixels->isNumber() &&
                                compared_pixels->asInt() >= 2 * result.clock.probe_count &&
                                checkpoint_digest != nullptr && checkpoint_digest->isString() &&
                                rerender_a != nullptr && rerender_a->isString() &&
                                rerender_b != nullptr && rerender_b->isString() &&
                                checkpoint_file != nullptr && checkpoint_file->isString();
                    if (resume_ok) {
                        const std::filesystem::path checkpoint_path =
                            std::filesystem::path(path).parent_path() /
                            checkpoint_file->asString();
                        LoadedCheckpoint reloaded;
                        std::string checkpoint_error;
                        if (!loadCheckpointArtifact(checkpoint_path.string(), reloaded,
                                                    checkpoint_error)) {
                            resume_ok = false;
                            resume_detail = "checkpoint reload failed: " + checkpoint_error;
                        } else if (reloaded.artifact_digest != checkpoint_digest->asString()) {
                            resume_ok = false;
                            resume_detail = "checkpoint digest differs from resume evidence";
                        } else if (reloaded.next_step != result.resume_start_step) {
                            resume_ok = false;
                            resume_detail = "checkpoint next_step differs from resume boundary";
                        } else {
                            const int checkpoint_step_value =
                                static_cast<int>(checkpoint_step->asInt());
                            for (size_t device = 0; device < result.devices.size(); ++device) {
                                const auto& run_frames = result.devices[device].frames;
                                const auto& prefix_frames =
                                    reloaded.prefix.devices[device].frames;
                                for (int step = 0;
                                     step < result.resume_start_step &&
                                     step < static_cast<int>(run_frames.size()) &&
                                     step < static_cast<int>(prefix_frames.size());
                                     ++step) {
                                    if (run_frames[step].frame_digest !=
                                        prefix_frames[step].frame_digest) {
                                        resume_ok = false;
                                        resume_detail = "resumed prefix differs from checkpoint";
                                    }
                                }
                                if (checkpoint_step_value >= 0 &&
                                    checkpoint_step_value <
                                        static_cast<int>(run_frames.size())) {
                                    const std::string& stored =
                                        device == 0 ? rerender_a->asString()
                                                    : rerender_b->asString();
                                    if (run_frames[checkpoint_step_value].frame_digest != stored) {
                                        resume_ok = false;
                                        resume_detail =
                                            "rerender digest differs from checkpoint frame";
                                    }
                                } else {
                                    resume_ok = false;
                                    resume_detail = "checkpoint frame is outside the run";
                                }
                            }
                        }
                    }
                    if (resume_ok) {
                        resume_detail = "checkpoint digest, rerender digest and every pixel match";
                    }
                }
            }
            add("resume_evidence", resume_ok, resume_detail);

            const Json* comparison = root.find("continuous_comparison");
            bool comparison_ok = true;
            std::string comparison_detail = "not requested";
            if (comparison != nullptr) {
                const Json* evaluated = comparison->find("evaluated");
                const Json* match = comparison->find("match");
                if (evaluated != nullptr && evaluated->isBool() && evaluated->asBool()) {
                    const Json* reference_path = comparison->find("reference_path");
                    const Json* reference_digest = comparison->find("reference_digest");
                    const Json* current_digest = comparison->find("current_digest");
                    comparison_ok = match != nullptr && match->isBool() && match->asBool() &&
                                    reference_path != nullptr && reference_path->isString() &&
                                    reference_digest != nullptr &&
                                    reference_digest->isString() && current_digest != nullptr &&
                                    current_digest->isString();
                    if (comparison_ok) {
                        const std::string actual_current = streamDigest(result);
                        comparison_ok = current_digest->asString() == actual_current;
                        if (comparison_ok) {
                            const std::filesystem::path resolved =
                                std::filesystem::path(path).parent_path() /
                                reference_path->asString();
                            std::string reference_raw;
                            Json reference_root;
                            std::string reference_error;
                            RunResult reference;
                            if (!loadJsonFile(resolved.string(), reference_raw, reference_root,
                                              reference_error) ||
                                !parseRunData(reference_root, reference, reference_error)) {
                                comparison_ok = false;
                                comparison_detail =
                                    "reference artifact could not be loaded: " + reference_error;
                            } else {
                                const std::string actual_reference =
                                    streamDigest(reference);
                                comparison_ok =
                                    actual_reference == reference_digest->asString() &&
                                    actual_reference == actual_current;
                                comparison_detail =
                                    comparison_ok
                                        ? "paused/resumed stream equals the independent continuous "
                                          "artifact"
                                        : "reference digest does not equal the paused stream";
                            }
                        } else {
                            comparison_detail = "stored current_digest does not recompute";
                        }
                    } else {
                        comparison_detail = "continuous comparison fields are missing";
                    }
                    comparison_detail = comparison_ok
                                            ? "paused/resumed result equals the continuous run"
                                            : comparison_detail;
                }
            }
            add("continuous_run_comparison", comparison_ok, comparison_detail);
        }
    } else {
        add("known_schema", false, "unknown artifact schema");
    }

    Json report = Json::object();
    report.set("schema", Json("pixel-history.review"));
    report.set("schema_version", Json(1));
    report.set("artifact", Json(path));
    report.set("artifact_digest", Json(digest));
    report.set("ok", Json(all_ok));
    report.set("checks", std::move(checks));
    report.set("review_formula",
               Json("recompute ticks with clock_model.formulas, then evaluate the four orbiting "
                    "squares and compare each probe RGBA/object_id"));
    report_json = report.dumpPretty(2);
    if (!all_ok) {
        error = "artifact verification found failed checks";
    }
    return all_ok;
}

std::string summarizeRunJson(const RunResult& result) {
    Json root = Json::object();
    root.set("schema", Json("pixel-history.summary"));
    root.set("run_id", Json(result.run_id));
    root.set("mode", Json(result.mode));
    root.set("status", Json(result.status));
    root.set("quality", Json(result.plan.effective_quality));
    root.set("resolution", Json(std::to_string(result.plan.width) + "x" +
                                std::to_string(result.plan.height)));
    root.set("degradations", Json(static_cast<int>(result.plan.degradations.size())));
    root.set("stream_digest", Json(streamDigest(result)));
    root.set("pre_pause_digest", Json(result.pre_summary.sample_digest));
    root.set("post_resume_digest", Json(result.post_summary.sample_digest));
    root.set("resume_match", Json(!result.resume.loaded || result.resume.match));
    root.set("continuous_match",
             Json(!result.comparison.evaluated || result.comparison.match));
    return root.dumpPretty(2);
}

std::string summarizeCheckpointJson(const RunResult& result) {
    Json root = Json::object();
    root.set("schema", Json("pixel-history.checkpoint-summary"));
    root.set("run_id", Json(result.run_id));
    root.set("status", Json(result.status));
    root.set("next_step", Json(result.next_step));
    root.set("frame_count", Json(result.frame_count));
    root.set("quality", Json(result.plan.effective_quality));
    root.set("stream_digest", Json(streamDigest(result)));
    root.set("checkpoint_file", Json(result.checkpoint_path));
    return root.dumpPretty(2);
}

}  // namespace ph
