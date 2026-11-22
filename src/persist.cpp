#include "skincheck/persist.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <stdexcept>
#include <sstream>
#include <string_view>

#include "skincheck/core.hpp"
#include "skincheck/sha256.hpp"

namespace skincheck {
namespace {

namespace fs = std::filesystem;

const std::vector<std::string> kRunFields = {
    "schema",          "run_id",       "created_utc", "phase",
    "capture",         "parameters",   "timeline",    "resource",
    "checkpoint",      "phases",       "comparison",  "summary",
    "frames",          "evidence",     "artifacts",   "record_order",
    "integrity",
};

const std::vector<std::string> kCheckpointFields = {
    "schema",        "run_id",       "phase",       "pause_reason",
    "capture_sha256", "capture",     "policy",      "resources",
    "timeline",      "next_frame",   "completed_frames", "summary",
};

const std::vector<std::string> kFrameFields = {
    "frame",      "reference_us", "present_devices", "missing_devices",
    "vertices_checked", "influences_checked", "status", "input_sha256",
    "result_sha256", "issues",
};

const std::vector<std::string> kIssueFields = {
    "code", "severity", "frame", "device_id", "vertex_id",
    "bone_id", "actual", "limit", "detail",
};

const Json& required(const Json& object, std::string_view key, std::string_view context) {
    const Json* value = object.find(key);
    if (value == nullptr) {
        throw std::runtime_error(std::string(context) + " is missing field '" +
                                 std::string(key) + "'");
    }
    return *value;
}

void expect_fields(const Json& object, const std::vector<std::string>& expected,
                   const std::string& context) {
    if (!object.is_object()) throw std::runtime_error(context + " must be an object");
    const std::vector<std::string> actual = object.keys();
    if (actual != expected) {
        std::string message = context + " field order mismatch; expected [";
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (i != 0) message += ",";
            message += expected[i];
        }
        message += "], actual [";
        for (std::size_t i = 0; i < actual.size(); ++i) {
            if (i != 0) message += ",";
            message += actual[i];
        }
        message += "]";
        throw std::runtime_error(message);
    }
}

Json strings_to_json(const std::vector<std::string>& values) {
    Json array = Json::array();
    for (const std::string& value : values) array.push(Json(value));
    return array;
}

std::vector<std::string> strings_from_json(const Json& array, const std::string& context) {
    if (!array.is_array()) throw std::runtime_error(context + " must be an array");
    std::vector<std::string> values;
    for (const Json& item : array.items()) {
        if (!item.is_string()) throw std::runtime_error(context + " must contain strings");
        values.push_back(item.as_string());
    }
    return values;
}

Json policy_to_json(const ResourcePolicy& policy) {
    Json json = Json::object();
    json.set("memory_budget_bytes", Json(policy.memory_budget_bytes));
    json.set("max_influences", Json(static_cast<std::int64_t>(policy.max_influences)));
    json.set("max_bone_id", Json(static_cast<std::int64_t>(policy.max_bone_id)));
    json.set("weight_epsilon", Json(policy.weight_epsilon));
    json.set("min_gap_frames", Json(policy.min_gap_frames));
    json.set("allow_degraded", Json(policy.allow_degraded));
    return json;
}

ResourcePolicy policy_from_json(const Json& json) {
    expect_fields(json, {"memory_budget_bytes", "max_influences", "max_bone_id",
                         "weight_epsilon", "min_gap_frames", "allow_degraded"},
                  "policy");
    ResourcePolicy policy;
    policy.memory_budget_bytes = required(json, "memory_budget_bytes", "policy").as_int64();
    policy.max_influences =
        static_cast<std::uint32_t>(required(json, "max_influences", "policy").as_int64());
    policy.max_bone_id =
        static_cast<std::uint32_t>(required(json, "max_bone_id", "policy").as_int64());
    policy.weight_epsilon = required(json, "weight_epsilon", "policy").as_double();
    policy.min_gap_frames = static_cast<int>(required(json, "min_gap_frames", "policy").as_int64());
    policy.allow_degraded = required(json, "allow_degraded", "policy").as_bool();
    return policy;
}

Json resource_to_json(const ResourcePlan& resource) {
    Json json = Json::object();
    json.set("mode", Json(resource_mode_name(resource.mode)));
    json.set("budget_bytes", Json(resource.memory_budget_bytes));
    json.set("estimated_full_bytes", Json(resource.estimated_full_bytes));
    json.set("max_influences", Json(static_cast<std::int64_t>(resource.max_influences)));
    json.set("max_bone_id", Json(static_cast<std::int64_t>(resource.max_bone_id)));
    json.set("weight_epsilon", Json(resource.weight_epsilon));
    json.set("skipped_checks", strings_to_json(resource.skipped_checks));
    json.set("reason", Json(resource.reason));
    return json;
}

ResourcePlan resource_from_json(const Json& json) {
    expect_fields(json, {"mode", "budget_bytes", "estimated_full_bytes", "max_influences",
                         "max_bone_id", "weight_epsilon", "skipped_checks", "reason"},
                  "resource");
    ResourcePlan resource;
    const std::string mode = required(json, "mode", "resource").as_string();
    if (mode == "full") resource.mode = ResourceMode::Full;
    else if (mode == "cpu_streaming_degraded") resource.mode = ResourceMode::Degraded;
    else throw std::runtime_error("unknown resource mode: " + mode);
    resource.memory_budget_bytes = required(json, "budget_bytes", "resource").as_int64();
    resource.estimated_full_bytes =
        required(json, "estimated_full_bytes", "resource").as_int64();
    resource.max_influences =
        static_cast<std::uint32_t>(required(json, "max_influences", "resource").as_int64());
    resource.max_bone_id =
        static_cast<std::uint32_t>(required(json, "max_bone_id", "resource").as_int64());
    resource.weight_epsilon = required(json, "weight_epsilon", "resource").as_double();
    resource.skipped_checks =
        strings_from_json(required(json, "skipped_checks", "resource"), "resource.skipped_checks");
    resource.reason = required(json, "reason", "resource").as_string();
    return resource;
}

Json summary_to_json(const Summary& summary) {
    Json json = Json::object();
    json.set("frames", Json(summary.frames));
    json.set("pass", Json(summary.pass));
    json.set("fail", Json(summary.fail));
    json.set("incomplete", Json(summary.incomplete));
    json.set("degraded", Json(summary.degraded));
    json.set("issues", Json(summary.issues));
    json.set("status", Json(summary.status));
    return json;
}

Summary summary_from_json(const Json& json) {
    expect_fields(json, {"frames", "pass", "fail", "incomplete", "degraded", "issues", "status"},
                  "summary");
    Summary summary;
    summary.frames = static_cast<int>(required(json, "frames", "summary").as_int64());
    summary.pass = static_cast<int>(required(json, "pass", "summary").as_int64());
    summary.fail = static_cast<int>(required(json, "fail", "summary").as_int64());
    summary.incomplete = static_cast<int>(required(json, "incomplete", "summary").as_int64());
    summary.degraded = static_cast<int>(required(json, "degraded", "summary").as_int64());
    summary.issues = static_cast<int>(required(json, "issues", "summary").as_int64());
    summary.status = required(json, "status", "summary").as_string();
    return summary;
}

Json clock_to_json(const ClockProjection& clock) {
    Json json = Json::object();
    json.set("device_id", Json(clock.device_id));
    json.set("offset_us", Json(clock.offset_us));
    json.set("drift_ppm", Json(clock.drift_ppm));
    json.set("uncertainty_us", Json(clock.uncertainty_us));
    json.set("first_reference_us", Json(clock.first_reference_us));
    json.set("last_reference_us", Json(clock.last_reference_us));
    return json;
}

ClockProjection clock_from_json(const Json& json) {
    expect_fields(json, {"device_id", "offset_us", "drift_ppm", "uncertainty_us",
                         "first_reference_us", "last_reference_us"},
                  "clock");
    ClockProjection clock;
    clock.device_id = required(json, "device_id", "clock").as_string();
    clock.offset_us = required(json, "offset_us", "clock").as_int64();
    clock.drift_ppm = required(json, "drift_ppm", "clock").as_double();
    clock.uncertainty_us = required(json, "uncertainty_us", "clock").as_int64();
    clock.first_reference_us = required(json, "first_reference_us", "clock").as_int64();
    clock.last_reference_us = required(json, "last_reference_us", "clock").as_int64();
    return clock;
}

Json missing_segment_to_json(const MissingSegment& segment) {
    Json json = Json::object();
    json.set("device_id", Json(segment.device_id));
    json.set("first_frame", Json(segment.first_frame));
    json.set("last_frame", Json(segment.last_frame));
    json.set("reference_start_us", Json(segment.reference_start_us));
    json.set("reference_end_us", Json(segment.reference_end_us));
    json.set("observed_local_gap_us", Json(segment.observed_local_gap_us));
    json.set("nominal_local_gap_us", Json(segment.nominal_local_gap_us));
    json.set("expected_local_gap_us", Json(segment.expected_local_gap_us));
    json.set("clock_skew_effect_us", Json(segment.clock_skew_effect_us));
    json.set("boundary_uncertainty_us", Json(segment.boundary_uncertainty_us));
    json.set("reason", Json(segment.reason));
    return json;
}

MissingSegment missing_segment_from_json(const Json& json) {
    expect_fields(json, {"device_id", "first_frame", "last_frame", "reference_start_us",
                         "reference_end_us", "observed_local_gap_us",
                         "nominal_local_gap_us", "expected_local_gap_us",
                         "clock_skew_effect_us", "boundary_uncertainty_us", "reason"},
                  "missing_segment");
    MissingSegment segment;
    segment.device_id = required(json, "device_id", "missing_segment").as_string();
    segment.first_frame = static_cast<int>(required(json, "first_frame", "missing_segment").as_int64());
    segment.last_frame = static_cast<int>(required(json, "last_frame", "missing_segment").as_int64());
    segment.reference_start_us =
        required(json, "reference_start_us", "missing_segment").as_int64();
    segment.reference_end_us =
        required(json, "reference_end_us", "missing_segment").as_int64();
    segment.observed_local_gap_us =
        required(json, "observed_local_gap_us", "missing_segment").as_int64();
    segment.nominal_local_gap_us =
        required(json, "nominal_local_gap_us", "missing_segment").as_int64();
    segment.expected_local_gap_us =
        required(json, "expected_local_gap_us", "missing_segment").as_int64();
    segment.clock_skew_effect_us =
        required(json, "clock_skew_effect_us", "missing_segment").as_int64();
    segment.boundary_uncertainty_us =
        required(json, "boundary_uncertainty_us", "missing_segment").as_int64();
    segment.reason = required(json, "reason", "missing_segment").as_string();
    return segment;
}

Json timeline_to_json(const Timeline& timeline) {
    Json json = Json::object();
    json.set("slot_count", Json(static_cast<std::int64_t>(
                               timeline.slot_count > 0
                                   ? timeline.slot_count
                                   : static_cast<int>(timeline.slots.size()))));
    json.set("reference_origin_us", Json(timeline.reference_origin_us));
    json.set("frame_period_us", Json(timeline.frame_period_us));
    Json clocks = Json::array();
    for (const ClockProjection& clock : timeline.clocks) clocks.push(clock_to_json(clock));
    json.set("clocks", std::move(clocks));
    Json missing = Json::array();
    for (const MissingSegment& segment : timeline.missing_segments) {
        missing.push(missing_segment_to_json(segment));
    }
    json.set("missing_segments", std::move(missing));
    return json;
}

Timeline timeline_from_json(const Json& json) {
    expect_fields(json, {"slot_count", "reference_origin_us", "frame_period_us", "clocks",
                         "missing_segments"},
                  "timeline");
    Timeline timeline;
    timeline.slot_count = static_cast<int>(required(json, "slot_count", "timeline").as_int64());
    timeline.reference_origin_us = required(json, "reference_origin_us", "timeline").as_int64();
    timeline.frame_period_us = required(json, "frame_period_us", "timeline").as_int64();
    const Json& clocks = required(json, "clocks", "timeline");
    if (!clocks.is_array()) throw std::runtime_error("timeline.clocks must be an array");
    for (const Json& clock : clocks.items()) timeline.clocks.push_back(clock_from_json(clock));
    const Json& missing = required(json, "missing_segments", "timeline");
    if (!missing.is_array()) throw std::runtime_error("timeline.missing_segments must be an array");
    for (const Json& segment : missing.items()) {
        timeline.missing_segments.push_back(missing_segment_from_json(segment));
    }
    return timeline;
}

}  // namespace

const std::vector<std::string>& run_field_order() { return kRunFields; }
const std::vector<std::string>& checkpoint_field_order() { return kCheckpointFields; }
const std::vector<std::string>& frame_field_order() { return kFrameFields; }
const std::vector<std::string>& issue_field_order() { return kIssueFields; }

Json capture_to_json(const Capture& capture) {
    Json json = Json::object();
    json.set("schema", Json(capture.schema));
    json.set("asset_id", Json(capture.asset_id));
    json.set("frame_period_us", Json(capture.frame_period_us));
    Json devices = Json::array();
    for (const DeviceCapture& device : capture.devices) {
        Json device_json = Json::object();
        device_json.set("device_id", Json(device.device_id));
        Json clock = Json::object();
        clock.set("offset_us", Json(device.clock.offset_us));
        clock.set("drift_ppm", Json(device.clock.drift_ppm));
        clock.set("uncertainty_us", Json(device.clock.uncertainty_us));
        device_json.set("clock", std::move(clock));
        Json frames = Json::array();
        for (const DeviceFrame& frame : device.frames) {
            Json frame_json = Json::object();
            frame_json.set("sequence", Json(frame.sequence));
            frame_json.set("local_time_us", Json(frame.local_time_us));
            Json vertices = Json::array();
            for (const Vertex& vertex : frame.vertices) {
                Json vertex_json = Json::object();
                vertex_json.set("vertex_id", Json(static_cast<std::int64_t>(vertex.vertex_id)));
                Json weights = Json::array();
                for (const Weight& weight : vertex.weights) {
                    Json weight_json = Json::object();
                    weight_json.set("bone_id", Json(static_cast<std::int64_t>(weight.bone_id)));
                    weight_json.set("value", Json(weight.value));
                    weights.push(std::move(weight_json));
                }
                vertex_json.set("weights", std::move(weights));
                vertices.push(std::move(vertex_json));
            }
            frame_json.set("vertices", std::move(vertices));
            frames.push(std::move(frame_json));
        }
        device_json.set("frames", std::move(frames));
        devices.push(std::move(device_json));
    }
    json.set("devices", std::move(devices));
    return json;
}

Capture capture_from_json(const Json& json) {
    expect_fields(json, {"schema", "asset_id", "frame_period_us", "devices"}, "capture");
    Capture capture;
    capture.schema = required(json, "schema", "capture").as_string();
    if (capture.schema != "skincheck.capture/v1") {
        throw std::runtime_error("unsupported capture schema: " + capture.schema);
    }
    capture.asset_id = required(json, "asset_id", "capture").as_string();
    capture.frame_period_us = required(json, "frame_period_us", "capture").as_int64();
    const Json& devices = required(json, "devices", "capture");
    if (!devices.is_array()) throw std::runtime_error("capture.devices must be an array");
    for (const Json& device_json : devices.items()) {
        expect_fields(device_json, {"device_id", "clock", "frames"}, "device");
        DeviceCapture device;
        device.device_id = required(device_json, "device_id", "device").as_string();
        const Json& clock = required(device_json, "clock", "device");
        expect_fields(clock, {"offset_us", "drift_ppm", "uncertainty_us"}, "device.clock");
        device.clock.offset_us = required(clock, "offset_us", "device.clock").as_int64();
        device.clock.drift_ppm = required(clock, "drift_ppm", "device.clock").as_double();
        device.clock.uncertainty_us =
            required(clock, "uncertainty_us", "device.clock").as_int64();
        const Json& frames = required(device_json, "frames", "device");
        if (!frames.is_array()) throw std::runtime_error("device.frames must be an array");
        for (const Json& frame_json : frames.items()) {
            expect_fields(frame_json, {"sequence", "local_time_us", "vertices"}, "frame");
            DeviceFrame frame;
            frame.sequence = required(frame_json, "sequence", "frame").as_int64();
            frame.local_time_us = required(frame_json, "local_time_us", "frame").as_int64();
            const Json& vertices = required(frame_json, "vertices", "frame");
            if (!vertices.is_array()) throw std::runtime_error("frame.vertices must be an array");
            for (const Json& vertex_json : vertices.items()) {
                expect_fields(vertex_json, {"vertex_id", "weights"}, "vertex");
                Vertex vertex;
                vertex.vertex_id = static_cast<std::uint32_t>(
                    required(vertex_json, "vertex_id", "vertex").as_int64());
                const Json& weights = required(vertex_json, "weights", "vertex");
                if (!weights.is_array()) throw std::runtime_error("vertex.weights must be an array");
                for (const Json& weight_json : weights.items()) {
                    expect_fields(weight_json, {"bone_id", "value"}, "weight");
                    Weight weight;
                    weight.bone_id = static_cast<std::uint32_t>(
                        required(weight_json, "bone_id", "weight").as_int64());
                    weight.value = required(weight_json, "value", "weight").as_double();
                    vertex.weights.push_back(weight);
                }
                frame.vertices.push_back(std::move(vertex));
            }
            device.frames.push_back(std::move(frame));
        }
        capture.devices.push_back(std::move(device));
    }
    return capture;
}

Json issue_to_json(const Issue& issue) {
    Json json = Json::object();
    json.set("code", Json(issue.code));
    json.set("severity", Json(issue.severity));
    json.set("frame", Json(issue.frame));
    json.set("device_id", Json(issue.device_id));
    json.set("vertex_id", Json(issue.vertex_id));
    json.set("bone_id", Json(issue.bone_id));
    json.set("actual", Json(issue.actual));
    json.set("limit", Json(issue.limit));
    json.set("detail", Json(issue.detail));
    return json;
}

Issue issue_from_json(const Json& json) {
    expect_fields(json, kIssueFields, "issue");
    Issue issue;
    issue.code = required(json, "code", "issue").as_string();
    issue.severity = required(json, "severity", "issue").as_string();
    issue.frame = static_cast<int>(required(json, "frame", "issue").as_int64());
    issue.device_id = required(json, "device_id", "issue").as_string();
    issue.vertex_id = required(json, "vertex_id", "issue").as_int64();
    issue.bone_id = required(json, "bone_id", "issue").as_int64();
    issue.actual = required(json, "actual", "issue").as_double();
    issue.limit = required(json, "limit", "issue").as_double();
    issue.detail = required(json, "detail", "issue").as_string();
    return issue;
}

Json frame_to_json(const FrameResult& frame) {
    Json json = Json::object();
    json.set("frame", Json(frame.frame));
    json.set("reference_us", Json(frame.reference_us));
    json.set("present_devices", strings_to_json(frame.present_devices));
    json.set("missing_devices", strings_to_json(frame.missing_devices));
    json.set("vertices_checked", Json(frame.vertices_checked));
    json.set("influences_checked", Json(frame.influences_checked));
    json.set("status", Json(frame.status));
    json.set("input_sha256", Json(frame.input_sha256));
    json.set("result_sha256", Json(frame.result_sha256));
    Json issues = Json::array();
    for (const Issue& issue : frame.issues) issues.push(issue_to_json(issue));
    json.set("issues", std::move(issues));
    return json;
}

FrameResult frame_from_json(const Json& json) {
    expect_fields(json, kFrameFields, "frame");
    FrameResult frame;
    frame.frame = static_cast<int>(required(json, "frame", "frame").as_int64());
    frame.reference_us = required(json, "reference_us", "frame").as_int64();
    frame.present_devices =
        strings_from_json(required(json, "present_devices", "frame"), "frame.present_devices");
    frame.missing_devices =
        strings_from_json(required(json, "missing_devices", "frame"), "frame.missing_devices");
    frame.vertices_checked =
        static_cast<int>(required(json, "vertices_checked", "frame").as_int64());
    frame.influences_checked =
        static_cast<int>(required(json, "influences_checked", "frame").as_int64());
    frame.status = required(json, "status", "frame").as_string();
    frame.input_sha256 = required(json, "input_sha256", "frame").as_string();
    frame.result_sha256 = required(json, "result_sha256", "frame").as_string();
    const Json& issues = required(json, "issues", "frame");
    if (!issues.is_array()) throw std::runtime_error("frame.issues must be an array");
    for (const Json& issue : issues.items()) frame.issues.push_back(issue_from_json(issue));
    return frame;
}

Json checkpoint_to_json(const Checkpoint& checkpoint) {
    Json json = Json::object();
    json.set("schema", Json(checkpoint.schema));
    json.set("run_id", Json(checkpoint.run_id));
    json.set("phase", Json(checkpoint.phase));
    json.set("pause_reason", Json(checkpoint.pause_reason));
    json.set("capture_sha256", Json(checkpoint.capture_sha256));
    json.set("capture", capture_to_json(checkpoint.capture));
    json.set("policy", policy_to_json(checkpoint.policy));
    json.set("resources", resource_to_json(checkpoint.resources));
    json.set("timeline", timeline_to_json(checkpoint.timeline));
    json.set("next_frame", Json(checkpoint.next_frame));
    Json frames = Json::array();
    for (const FrameResult& frame : checkpoint.completed_frames) {
        frames.push(frame_to_json(frame));
    }
    json.set("completed_frames", std::move(frames));
    json.set("summary", summary_to_json(checkpoint.summary));
    return json;
}

Checkpoint checkpoint_from_json(const Json& json) {
    expect_fields(json, kCheckpointFields, "checkpoint");
    Checkpoint checkpoint;
    checkpoint.schema = required(json, "schema", "checkpoint").as_string();
    if (checkpoint.schema != "skincheck.checkpoint/v1") {
        throw std::runtime_error("unsupported checkpoint schema: " + checkpoint.schema);
    }
    checkpoint.run_id = required(json, "run_id", "checkpoint").as_string();
    checkpoint.phase = required(json, "phase", "checkpoint").as_string();
    checkpoint.pause_reason = required(json, "pause_reason", "checkpoint").as_string();
    checkpoint.capture_sha256 =
        required(json, "capture_sha256", "checkpoint").as_string();
    checkpoint.capture = capture_from_json(required(json, "capture", "checkpoint"));
    checkpoint.policy = policy_from_json(required(json, "policy", "checkpoint"));
    checkpoint.resources = resource_from_json(required(json, "resources", "checkpoint"));
    checkpoint.timeline = timeline_from_json(required(json, "timeline", "checkpoint"));
    checkpoint.next_frame =
        static_cast<int>(required(json, "next_frame", "checkpoint").as_int64());
    const Json& frames = required(json, "completed_frames", "checkpoint");
    if (!frames.is_array()) throw std::runtime_error("checkpoint.completed_frames must be an array");
    for (const Json& frame : frames.items()) {
        checkpoint.completed_frames.push_back(frame_from_json(frame));
    }
    checkpoint.summary = summary_from_json(required(json, "summary", "checkpoint"));
    return checkpoint;
}

Json comparison_to_json(const ResumeComparison& comparison) {
    Json json = Json::object();
    json.set("frames_rechecked", Json(comparison.frames_rechecked));
    json.set("matched", Json(comparison.matched));
    json.set("mismatched", Json(comparison.mismatched));
    json.set("new_frames", Json(comparison.new_frames));
    json.set("stable", Json(comparison.stable));
    json.set("before_status", Json(comparison.before_status));
    json.set("after_status", Json(comparison.after_status));
    Json frames = Json::array();
    for (const FrameComparison& item : comparison.frames) {
        Json frame = Json::object();
        frame.set("frame", Json(item.frame));
        frame.set("matched", Json(item.matched));
        frame.set("checkpoint_result_sha256", Json(item.checkpoint_result_sha256));
        frame.set("recomputed_result_sha256", Json(item.recomputed_result_sha256));
        frames.push(std::move(frame));
    }
    json.set("frames", std::move(frames));
    return json;
}

Json evidence_to_json(const RunSnapshot& snapshot) {
    Json evidence = Json::array();
    for (const MissingSegment& segment : snapshot.timeline.missing_segments) {
        Json item = Json::object();
        item.set("kind", Json("missing_segment"));
        item.set("device_id", Json(segment.device_id));
        item.set("first_frame", Json(segment.first_frame));
        item.set("last_frame", Json(segment.last_frame));
        item.set("reference_start_us", Json(segment.reference_start_us));
        item.set("reference_end_us", Json(segment.reference_end_us));
        item.set("observed_local_gap_us", Json(segment.observed_local_gap_us));
        item.set("nominal_local_gap_us", Json(segment.nominal_local_gap_us));
        item.set("expected_local_gap_us", Json(segment.expected_local_gap_us));
        item.set("clock_skew_effect_us", Json(segment.clock_skew_effect_us));
        item.set("boundary_uncertainty_us", Json(segment.boundary_uncertainty_us));
        item.set("reason", Json(segment.reason));
        evidence.push(std::move(item));
    }
    for (const ClockProjection& clock : snapshot.timeline.clocks) {
        Json item = Json::object();
        item.set("kind", Json("clock_projection"));
        item.set("device_id", Json(clock.device_id));
        item.set("offset_us", Json(clock.offset_us));
        item.set("drift_ppm", Json(clock.drift_ppm));
        item.set("uncertainty_us", Json(clock.uncertainty_us));
        item.set("first_reference_us", Json(clock.first_reference_us));
        item.set("last_reference_us", Json(clock.last_reference_us));
        evidence.push(std::move(item));
    }
    Json resource_item = Json::object();
    resource_item.set("kind", Json(snapshot.resources.mode == ResourceMode::Degraded
                                       ? "resource_degradation"
                                       : "resource_plan"));
    resource_item.set("budget_bytes", Json(snapshot.resources.memory_budget_bytes));
    resource_item.set("estimated_full_bytes", Json(snapshot.resources.estimated_full_bytes));
    resource_item.set("mode", Json(resource_mode_name(snapshot.resources.mode)));
    resource_item.set("reason", Json(snapshot.resources.reason));
    resource_item.set("skipped_checks",
                      strings_to_json(snapshot.resources.skipped_checks));
    evidence.push(std::move(resource_item));
    if (snapshot.has_checkpoint) {
        Json item = Json::object();
        item.set("kind", Json("checkpoint"));
        item.set("next_frame", Json(snapshot.checkpoint.next_frame));
        item.set("completed_frames", Json(static_cast<int>(snapshot.before_frames.size())));
        item.set("capture_sha256", Json(snapshot.checkpoint.capture_sha256));
        item.set("pause_reason", Json(snapshot.checkpoint.pause_reason));
        evidence.push(std::move(item));
    }
    const auto& evidence_frames = snapshot.final_frames.empty() ? snapshot.before_frames
                                                                : snapshot.final_frames;
    for (const FrameResult& frame : evidence_frames) {
        for (const Issue& issue : frame.issues) {
            Json item = issue_to_json(issue);
            Json wrapped = Json::object();
            wrapped.set("kind", Json("skin_issue"));
            for (const auto& field : item.fields()) wrapped.set(field.first, field.second);
            evidence.push(std::move(wrapped));
        }
    }
    if (snapshot.phase == "complete") {
        Json item = Json::object();
        item.set("kind", Json("resume_comparison"));
        item.set("frames_rechecked", Json(snapshot.comparison.frames_rechecked));
        item.set("matched", Json(snapshot.comparison.matched));
        item.set("mismatched", Json(snapshot.comparison.mismatched));
        item.set("new_frames", Json(snapshot.comparison.new_frames));
        item.set("stable", Json(snapshot.comparison.stable));
        evidence.push(std::move(item));
    }
    return evidence;
}

Json run_to_json(const RunSnapshot& snapshot,
                 const std::vector<std::pair<std::string, std::string>>& artifacts) {
    Json json = Json::object();
    json.set("schema", Json(snapshot.schema));
    json.set("run_id", Json(snapshot.run_id));
    json.set("created_utc", Json(snapshot.created_utc));
    json.set("phase", Json(snapshot.phase));
    json.set("capture", capture_to_json(snapshot.capture));
    json.set("parameters", policy_to_json(snapshot.policy));
    json.set("timeline", timeline_to_json(snapshot.timeline));
    json.set("resource", resource_to_json(snapshot.resources));

    Json checkpoint = Json::object();
    checkpoint.set("present", Json(snapshot.has_checkpoint));
    checkpoint.set("next_frame", Json(snapshot.has_checkpoint ? snapshot.checkpoint.next_frame : -1));
    checkpoint.set("completed_frames",
                   Json(static_cast<std::int64_t>(snapshot.before_frames.size())));
    checkpoint.set("capture_sha256",
                   Json(snapshot.has_checkpoint ? snapshot.checkpoint.capture_sha256 : ""));
    json.set("checkpoint", std::move(checkpoint));

    Json phases = Json::object();
    phases.set("before_resume", summary_to_json(summarize(snapshot.before_frames)));
    phases.set("after_resume",
               summary_to_json(snapshot.phase == "complete" ? summarize(snapshot.final_frames)
                                                            : Summary{}));
    json.set("phases", std::move(phases));
    json.set("comparison", comparison_to_json(snapshot.comparison));
    json.set("summary", summary_to_json(snapshot.phase == "complete"
                                            ? summarize(snapshot.final_frames)
                                            : summarize(snapshot.before_frames)));

    const auto& run_frames = snapshot.final_frames.empty() ? snapshot.before_frames
                                                           : snapshot.final_frames;
    Json frames = Json::array();
    for (const FrameResult& frame : run_frames) frames.push(frame_to_json(frame));
    json.set("frames", std::move(frames));
    json.set("evidence", evidence_to_json(snapshot));
    Json artifact_json = Json::array();
    for (const auto& artifact : artifacts) {
        Json item = Json::object();
        item.set("path", Json(artifact.first));
        item.set("sha256", Json(artifact.second));
        artifact_json.push(std::move(item));
    }
    json.set("artifacts", std::move(artifact_json));
    json.set("record_order", strings_to_json(kRunFields));
    Json integrity = Json::object();
    integrity.set("algorithm", Json("sha256"));
    integrity.set("capture_sha256", Json(sha256_hex(capture_to_json(snapshot.capture).dump(0))));
    std::string frames_sha256;
    for (const auto& artifact : artifacts) {
        if (artifact.first == "frames.jsonl") frames_sha256 = artifact.second;
    }
    integrity.set("frames_sha256", Json(frames_sha256));
    json.set("integrity", std::move(integrity));
    return json;
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path.string());
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.good() && !input.eof()) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return contents.str();
}

void write_atomic(const fs::path& path, const std::string& contents) {
    const fs::path temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot create " + temporary.string());
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!output) throw std::runtime_error("cannot write " + temporary.string());
    }
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary);
        throw std::runtime_error("cannot replace " + path.string() + ": " + error.message());
    }
}

std::string artifact_hash(const fs::path& directory, const std::string& name) {
    std::string error;
    const std::string hash = sha256_file_hex((directory / name).string(), &error);
    if (!error.empty()) throw std::runtime_error(error);
    return hash;
}

std::string markdown_escape(std::string value) {
    for (char& ch : value) {
        if (ch == '|') ch = '/';
        if (ch == '\n' || ch == '\r') ch = ' ';
    }
    return value;
}

std::string format_number(double value) {
    std::ostringstream out;
    out << std::setprecision(10) << value;
    return out.str();
}

std::string review_markdown(const RunSnapshot& snapshot,
                            const std::vector<std::pair<std::string, std::string>>& artifacts) {
    std::ostringstream out;
    const Summary summary = snapshot.phase == "complete" ? summarize(snapshot.final_frames)
                                                         : summarize(snapshot.before_frames);
    out << "# 动画蒙皮权重检查记录\n\n";
    out << "- run_id: `" << snapshot.run_id << "`\n";
    out << "- phase: `" << snapshot.phase << "`\n";
    out << "- asset: `" << snapshot.capture.asset_id << "`\n";
    out << "- status: `" << summary.status << "`\n";
    out << "- frames: " << summary.frames << ", pass: " << summary.pass
        << ", fail: " << summary.fail << ", incomplete: " << summary.incomplete
        << ", degraded: " << summary.degraded << ", issues: " << summary.issues << "\n\n";

    out << "## 复核命令\n\n";
    out << "```bash\nskincheck verify --run <本目录>\nsha256sum -c SHA256SUMS\n```\n\n";
    out << "复核器会重新解析 `capture.json`、按时钟重建参考时间线、重跑每个帧，"
           "并比对 `frames.jsonl` 中的 `result_sha256`。\n\n";

    out << "## 时间线与缺失片段\n\n";
    for (const ClockProjection& clock : snapshot.timeline.clocks) {
        out << "- 时钟 `" << clock.device_id << "`: offset=" << clock.offset_us
            << " us, drift=" << format_number(clock.drift_ppm)
            << " ppm, uncertainty=" << clock.uncertainty_us << " us\n";
    }
    if (snapshot.timeline.missing_segments.empty()) {
        out << "- 未检测到缺失片段。\n";
    } else {
        for (const MissingSegment& segment : snapshot.timeline.missing_segments) {
            out << "- 缺失 `" << segment.device_id << "` 帧 "
                << segment.first_frame << ".." << segment.last_frame << "，参考区间 ["
                << segment.reference_start_us << ", " << segment.reference_end_us
                << ") us；本地间隔 " << segment.observed_local_gap_us
                << " us，标称间隔 " << segment.nominal_local_gap_us
                << " us，时钟偏移影响 " << segment.clock_skew_effect_us
                << " us，投影预期 " << segment.expected_local_gap_us
                << " us，边界不确定度 " << segment.boundary_uncertainty_us << " us；原因 `"
                << segment.reason << "`。\n";
        }
    }
    out << "\n## 暂停与恢复\n\n";
    out << "- checkpoint next_frame: " << snapshot.checkpoint.next_frame << "\n";
    out << "- 已保存帧: " << snapshot.before_frames.size() << "\n";
    out << "- 重算帧: " << snapshot.comparison.frames_rechecked
        << ", 匹配: " << snapshot.comparison.matched
        << ", 不一致: " << snapshot.comparison.mismatched
        << ", 新增帧: " << snapshot.comparison.new_frames << "\n";
    out << "- stable: `" << (snapshot.comparison.stable ? "true" : "false") << "`\n\n";

    out << "## 资源路径\n\n";
    out << "- mode: `" << resource_mode_name(snapshot.resources.mode) << "`\n";
    out << "- budget: " << snapshot.resources.memory_budget_bytes << " bytes\n";
    out << "- estimated_full: " << snapshot.resources.estimated_full_bytes << " bytes\n";
    out << "- reason: `" << snapshot.resources.reason << "`\n";
    out << "- skipped_checks: ";
    if (snapshot.resources.skipped_checks.empty()) out << "none\n";
    else {
        for (std::size_t i = 0; i < snapshot.resources.skipped_checks.size(); ++i) {
            if (i != 0) out << ", ";
            out << "`" << snapshot.resources.skipped_checks[i] << "`";
        }
        out << "\n";
    }
    out << "\n## 问题证据\n\n";
    bool any_issue = false;
    const auto& review_frames = snapshot.final_frames.empty() ? snapshot.before_frames
                                                              : snapshot.final_frames;
    for (const FrameResult& frame : review_frames) {
        for (const Issue& issue : frame.issues) {
            any_issue = true;
            out << "- frame " << issue.frame << ", device `" << issue.device_id
                << "`, vertex " << issue.vertex_id << ", bone " << issue.bone_id
                << ", code `" << issue.code << "`, actual=" << format_number(issue.actual)
                << ", limit=" << format_number(issue.limit) << ": "
                << markdown_escape(issue.detail) << "\n";
        }
    }
    if (!any_issue) out << "- 无蒙皮权重问题。\n";

    out << "\n## 产物摘要\n\n";
    out << "| file | sha256 |\n|---|---|\n";
    for (const auto& artifact : artifacts) {
        out << "| `" << artifact.first << "` | `" << artifact.second << "` |\n";
    }
    return out.str();
}

ExportResult export_run(const std::string& directory, const RunSnapshot& snapshot) {
    ExportResult result;
    try {
        const fs::path root(directory);
        fs::create_directories(root);
        std::vector<std::pair<std::string, std::string>> artifacts;

        const std::string capture_text = capture_to_json(snapshot.capture).dump(2) + "\n";
        write_atomic(root / "capture.json", capture_text);
        artifacts.emplace_back("capture.json", sha256_hex(capture_text));

        if (snapshot.has_checkpoint) {
            const std::string checkpoint_text = checkpoint_to_json(snapshot.checkpoint).dump(2) + "\n";
            write_atomic(root / "checkpoint.json", checkpoint_text);
            artifacts.emplace_back("checkpoint.json", sha256_hex(checkpoint_text));
        }

        const auto& frames = snapshot.final_frames.empty() ? snapshot.before_frames
                                                           : snapshot.final_frames;
        std::string frames_text;
        int previous_frame = -1;
        for (const FrameResult& frame : frames) {
            if (frame.frame <= previous_frame) {
                throw std::runtime_error("frame order is not strictly increasing");
            }
            previous_frame = frame.frame;
            frames_text += frame_to_json(frame).dump(0);
            frames_text.push_back('\n');
        }
        write_atomic(root / "frames.jsonl", frames_text);
        artifacts.emplace_back("frames.jsonl", sha256_hex(frames_text));

        const Json evidence = evidence_to_json(snapshot);
        std::string evidence_text;
        for (const Json& item : evidence.items()) {
            evidence_text += item.dump(0);
            evidence_text.push_back('\n');
        }
        write_atomic(root / "evidence.jsonl", evidence_text);
        artifacts.emplace_back("evidence.jsonl", sha256_hex(evidence_text));

        const std::string review_text = review_markdown(snapshot, artifacts);
        write_atomic(root / "review.md", review_text);
        artifacts.emplace_back("review.md", sha256_hex(review_text));

        const std::string run_text = run_to_json(snapshot, artifacts).dump(2) + "\n";
        write_atomic(root / "run.json", run_text);

        std::string sums;
        for (const auto& artifact : artifacts) {
            sums += artifact.second + "  " + artifact.first + "\n";
        }
        sums += sha256_hex(run_text) + "  run.json\n";
        write_atomic(root / "SHA256SUMS", sums);

        result.files = {"capture.json"};
        if (snapshot.has_checkpoint) result.files.push_back("checkpoint.json");
        result.files.push_back("frames.jsonl");
        result.files.push_back("evidence.jsonl");
        result.files.push_back("review.md");
        result.files.push_back("run.json");
        result.files.push_back("SHA256SUMS");
        result.ok = true;
    } catch (const std::exception& exception) {
        result.error = exception.what();
    }
    return result;
}

namespace {

void record_check(VerifyResult& result, bool ok, const std::string& message) {
    ++result.checks;
    result.messages.push_back((ok ? "PASS: " : "FAIL: ") + message);
    if (!ok) ++result.failures;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

void write_verification(const fs::path& directory, const VerifyResult& result) {
    Json json = Json::object();
    json.set("schema", Json("skincheck.verification/v1"));
    json.set("ok", Json(result.ok));
    json.set("checks", Json(result.checks));
    json.set("failures", Json(result.failures));
    Json messages = Json::array();
    for (const std::string& message : result.messages) messages.push(Json(message));
    json.set("messages", std::move(messages));
    write_atomic(directory / "verification.json", json.dump(2) + "\n");
}

VerifyResult verify_run_impl(const std::string& directory) {
    VerifyResult result;
    const fs::path root(directory);
    const std::string raw_run = read_file(root / "run.json");
    std::string parse_error;
    const Json run = Json::parse(raw_run, &parse_error);
    record_check(result, parse_error.empty(), "run.json parses as JSON");
    if (!parse_error.empty() || !run.is_object()) {
        result.messages.push_back("parse detail: " + parse_error);
        return result;
    }

    record_check(result, run.dump(2) + "\n" == raw_run,
                 "run.json round-trips without field or order loss");
    record_check(result, run.keys() == kRunFields, "run.json has the documented field order");
    record_check(result, required(run, "schema", "run").as_string() == "skincheck.run/v1",
                 "run schema is skincheck.run/v1");
    const Json& record_order = required(run, "record_order", "run");
    record_check(result,
                 strings_from_json(record_order, "record_order") == kRunFields,
                 "record_order matches executable schema");

    const Capture capture = capture_from_json(required(run, "capture", "run"));
    const std::string canonical_capture = capture_to_json(capture).dump(0);
    const std::string capture_sha256 = sha256_hex(canonical_capture);
    record_check(result, capture_sha256 ==
                             required(required(run, "integrity", "run"),
                                      "capture_sha256", "integrity").as_string(),
                 "embedded capture hash matches canonical capture bytes");

    const std::string capture_file = read_file(root / "capture.json");
    std::string capture_parse_error;
    const Json capture_json = Json::parse(capture_file, &capture_parse_error);
    record_check(result, capture_parse_error.empty() &&
                             capture_json.dump(2) + "\n" == capture_file,
                 "capture.json round-trips without field or order loss");
    record_check(result, capture_json.dump(0) == canonical_capture,
                 "capture.json equals the embedded capture");

    const ResourcePolicy policy = policy_from_json(required(run, "parameters", "run"));
    const ResourcePlan recorded_resource = resource_from_json(required(run, "resource", "run"));
    const Timeline timeline = build_timeline(capture, policy.min_gap_frames);
    const Json recorded_timeline = required(run, "timeline", "run");
    record_check(result, timeline_to_json(timeline).dump(0) == recorded_timeline.dump(0),
                 "timeline independently rebuilds from capture and clock metadata");
    const ResourcePlan rebuilt_resource = plan_resources(capture, timeline, policy);
    record_check(result, resource_to_json(recorded_resource).dump(0) ==
                             resource_to_json(rebuilt_resource).dump(0),
                 "resource plan independently rebuilds from capture and policy");

    const std::string frames_file = read_file(root / "frames.jsonl");
    const std::vector<std::string> frame_lines = split_lines(frames_file);
    const Json& manifest_frames = required(run, "frames", "run");
    record_check(result, manifest_frames.is_array() &&
                             manifest_frames.items().size() == frame_lines.size(),
                 "manifest frame count matches frames.jsonl");
    bool frames_match = true;
    bool frames_recompute = true;
    std::vector<FrameResult> recomputed_frames;
    int previous_frame = -1;
    for (std::size_t i = 0; i < frame_lines.size(); ++i) {
        std::string error;
        const Json frame_json = Json::parse(frame_lines[i], &error);
        if (!error.empty() || frame_json.dump(0) != frame_lines[i]) {
            frames_match = false;
            break;
        }
        FrameResult frame;
        try {
            frame = frame_from_json(frame_json);
        } catch (const std::exception&) {
            frames_match = false;
            break;
        }
        if (frame.frame <= previous_frame) {
            frames_match = false;
            break;
        }
        previous_frame = frame.frame;
        if (i >= manifest_frames.items().size() ||
            manifest_frames.items()[i].dump(0) != frame_json.dump(0)) {
            frames_match = false;
            break;
        }
        const FrameResult recomputed = check_frame(timeline, frame.frame, rebuilt_resource);
        recomputed_frames.push_back(recomputed);
        if (frame_to_json(recomputed).dump(0) != frame_json.dump(0)) {
            frames_recompute = false;
            break;
        }
    }
    record_check(result, frames_match,
                 "frames.jsonl preserves fields/order and matches manifest");
    record_check(result, frames_recompute,
                 "every frame result_sha256 and evidence independently recomputes");
    if (frames_recompute) {
        record_check(result,
                     required(run, "summary", "run").dump(0) ==
                         summary_to_json(summarize(recomputed_frames)).dump(0),
                     "run summary recomputes from frame results");
    }

    const std::string evidence_file = read_file(root / "evidence.jsonl");
    bool evidence_roundtrip = true;
    for (const std::string& line : split_lines(evidence_file)) {
        std::string error;
        const Json item = Json::parse(line, &error);
        if (!error.empty() || item.dump(0) != line || item.find("kind") == nullptr) {
            evidence_roundtrip = false;
            break;
        }
    }
    record_check(result, evidence_roundtrip,
                 "evidence.jsonl preserves fields/order and every record has kind");

    const Json& comparison = required(run, "comparison", "run");
    if (required(run, "phase", "run").as_string() == "complete") {
        const bool stable = required(comparison, "stable", "comparison").as_bool();
        const std::int64_t matched = required(comparison, "matched", "comparison").as_int64();
        const std::int64_t mismatched =
            required(comparison, "mismatched", "comparison").as_int64();
        record_check(result, stable && mismatched == 0,
                     "resume comparison is stable with zero mismatches");
        record_check(result, matched > 0, "resume comparison contains rechecked frames");
    }

    const Json& artifacts = required(run, "artifacts", "run");
    record_check(result, artifacts.is_array(), "artifact list is an array");
    bool artifact_hashes = true;
    bool required_artifact_present = false;
    std::set<std::string> artifact_names;
    if (artifacts.is_array()) {
        for (const Json& item : artifacts.items()) {
            expect_fields(item, {"path", "sha256"}, "artifact");
            const std::string path = required(item, "path", "artifact").as_string();
            const std::string expected = required(item, "sha256", "artifact").as_string();
            artifact_names.insert(path);
            const fs::path relative(path);
            if (relative.is_absolute() ||
                std::find(relative.begin(), relative.end(), fs::path("..")) != relative.end()) {
                artifact_hashes = false;
            }
            std::string error;
            const std::string actual = sha256_file_hex((root / path).string(), &error);
            if (!error.empty() || actual != expected) artifact_hashes = false;
            if (path == "frames.jsonl") required_artifact_present = true;
        }
    }
    record_check(result, artifact_hashes, "all artifact hashes match manifest");
    record_check(result, required_artifact_present, "frames.jsonl is declared in manifest");
    record_check(result,
                 artifact_names == std::set<std::string>{"capture.json", "checkpoint.json",
                                                        "frames.jsonl", "evidence.jsonl",
                                                        "review.md"},
                 "manifest declares the complete fixed artifact set");

    const fs::path checkpoint_path = root / "checkpoint.json";
    if (fs::exists(checkpoint_path)) {
        try {
            const std::string checkpoint_text = read_file(checkpoint_path);
            std::string error;
            const Json checkpoint_json = Json::parse(checkpoint_text, &error);
            record_check(result, error.empty() && checkpoint_json.dump(2) + "\n" == checkpoint_text,
                         "checkpoint.json round-trips without field or order loss");
            const Checkpoint checkpoint = checkpoint_from_json(checkpoint_json);
            record_check(result,
                         checkpoint.capture_sha256 ==
                             sha256_hex(capture_to_json(checkpoint.capture).dump(0)),
                         "checkpoint capture hash matches embedded capture");
            bool prefix_match = checkpoint.completed_frames.size() <= frame_lines.size();
            for (std::size_t i = 0;
                 prefix_match && i < checkpoint.completed_frames.size(); ++i) {
                const Json expected =
                    frame_to_json(check_frame(timeline, checkpoint.completed_frames[i].frame,
                                              rebuilt_resource));
                prefix_match = expected.dump(0) ==
                               frame_to_json(checkpoint.completed_frames[i]).dump(0);
            }
            record_check(result, prefix_match,
                         "checkpoint completed frames match independent recomputation");
            const Json& declared_checkpoint = required(run, "checkpoint", "run");
            record_check(
                result,
                required(declared_checkpoint, "next_frame", "checkpoint").as_int64() ==
                        checkpoint.next_frame &&
                    required(declared_checkpoint, "completed_frames", "checkpoint").as_int64() ==
                        static_cast<std::int64_t>(checkpoint.completed_frames.size()) &&
                    required(declared_checkpoint, "capture_sha256", "checkpoint").as_string() ==
                        checkpoint.capture_sha256,
                "run checkpoint metadata matches checkpoint.json");
        } catch (const std::exception& exception) {
            record_check(result, false,
                         std::string("checkpoint review failed: ") + exception.what());
        }
    } else {
        record_check(result, false, "checkpoint.json is present");
    }

    const std::string sums_text = read_file(root / "SHA256SUMS");
    bool sums_ok = true;
    for (const std::string& line : split_lines(sums_text)) {
        if (line.size() < 66 || line[64] != ' ' || line[65] != ' ') {
            sums_ok = false;
            break;
        }
        const std::string expected = line.substr(0, 64);
        const std::string path = line.substr(66);
        std::string error;
        const std::string actual = sha256_file_hex((root / path).string(), &error);
        if (!error.empty() || actual != expected) {
            sums_ok = false;
            break;
        }
    }
    record_check(result, sums_ok, "SHA256SUMS verifies every named file");
    result.ok = result.failures == 0;
    return result;
}

}  // namespace

VerifyResult verify_run(const std::string& directory) {
    VerifyResult result;
    try {
        result = verify_run_impl(directory);
    } catch (const std::exception& exception) {
        result.ok = false;
        result.messages.push_back(std::string("FAIL: verifier error: ") + exception.what());
        ++result.checks;
        ++result.failures;
    }
    try {
        write_verification(fs::path(directory), result);
        result.report_path = (fs::path(directory) / "verification.json").string();
    } catch (const std::exception& exception) {
        result.ok = false;
        result.messages.push_back(std::string("FAIL: cannot write verification.json: ") +
                                  exception.what());
        ++result.failures;
    }
    return result;
}

}  // namespace skincheck
