#include "skincheck/core.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "skincheck/sha256.hpp"

namespace skincheck {
namespace {

std::string number_key(double value) {
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return out.str();
}

std::int64_t reference_to_slot(std::int64_t reference_us, std::int64_t period_us) {
    if (period_us <= 0) throw std::runtime_error("frame period must be positive");
    const long double slot = static_cast<long double>(reference_us) /
                             static_cast<long double>(period_us);
    return static_cast<std::int64_t>(std::llround(slot));
}

std::int64_t project_time(const DeviceClock& clock, std::int64_t local_time_us) {
    const double scale = 1.0 + clock.drift_ppm / 1000000.0;
    if (scale <= 0.0) throw std::runtime_error("clock drift makes projection invalid");
    return static_cast<std::int64_t>(
        std::llround(static_cast<double>(local_time_us - clock.offset_us) / scale));
}

std::string frame_input_digest(const TimelineSlot& slot) {
    Json vertices = Json::array();
    struct Entry {
        std::string device;
        const Vertex* vertex;
        std::size_t device_order;
    };
    std::vector<Entry> entries;
    for (std::size_t device_index = 0; device_index < slot.device_frames.size(); ++device_index) {
        const DeviceFrame* frame = slot.device_frames[device_index];
        for (const Vertex& vertex : frame->vertices) {
            entries.push_back({slot.present_devices[device_index], &vertex, device_index});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        if (left.vertex->vertex_id != right.vertex->vertex_id) {
            return left.vertex->vertex_id < right.vertex->vertex_id;
        }
        return left.device_order < right.device_order;
    });
    for (const Entry& entry : entries) {
        Json item = Json::object();
        item.set("device", Json(entry.device));
        item.set("vertex", Json(static_cast<std::int64_t>(entry.vertex->vertex_id)));
        Json weights = Json::array();
        for (const Weight& weight : entry.vertex->weights) {
            Json weight_json = Json::object();
            weight_json.set("bone", Json(static_cast<std::int64_t>(weight.bone_id)));
            weight_json.set("value", Json(number_key(weight.value)));
            weights.push(std::move(weight_json));
        }
        item.set("weights", std::move(weights));
        vertices.push(std::move(item));
    }
    return sha256_hex(vertices.dump(0));
}

std::string frame_result_digest(const FrameResult& result) {
    Json record = Json::object();
    record.set("frame", Json(result.frame));
    record.set("status", Json(result.status));
    record.set("vertices", Json(result.vertices_checked));
    record.set("influences", Json(result.influences_checked));
    Json present = Json::array();
    for (const auto& device : result.present_devices) present.push(Json(device));
    record.set("present", std::move(present));
    Json missing = Json::array();
    for (const auto& device : result.missing_devices) missing.push(Json(device));
    record.set("missing", std::move(missing));
    Json issues = Json::array();
    for (const Issue& issue : result.issues) {
        Json item = Json::object();
        item.set("code", Json(issue.code));
        item.set("device", Json(issue.device_id));
        item.set("vertex", Json(issue.vertex_id));
        item.set("bone", Json(issue.bone_id));
        item.set("actual", Json(number_key(issue.actual)));
        item.set("limit", Json(number_key(issue.limit)));
        issues.push(std::move(item));
    }
    record.set("issues", std::move(issues));
    return sha256_hex(record.dump(0));
}

void add_issue(FrameResult& result, std::string code, const std::string& device,
               std::int64_t vertex, std::int64_t bone, double actual, double limit,
               std::string detail) {
    Issue issue;
    issue.code = std::move(code);
    issue.frame = result.frame;
    issue.device_id = device;
    issue.vertex_id = vertex;
    issue.bone_id = bone;
    issue.actual = actual;
    issue.limit = limit;
    issue.detail = std::move(detail);
    result.issues.push_back(std::move(issue));
}

std::vector<Weight> base_weights(std::uint32_t vertex_id, int frame) {
    const std::uint32_t base = (vertex_id * 3U + static_cast<std::uint32_t>(frame)) % 61U;
    return {
        {base, 0.4},
        {base + 1U, 0.3},
        {base + 2U, 0.2},
        {base + 3U, 0.1},
    };
}

std::vector<Vertex> make_vertices(bool device_a, int frame) {
    std::vector<Vertex> vertices;
    const std::uint32_t first = device_a ? 0U : 8U;
    for (std::uint32_t offset = 0; offset < 8U; ++offset) {
        Vertex vertex;
        vertex.vertex_id = first + offset;
        vertex.weights = base_weights(vertex.vertex_id, frame);
        vertices.push_back(std::move(vertex));
    }

    if (device_a && frame == 3) vertices[2].weights[3].value = 0.0;
    if (!device_a && frame == 6) vertices[2].weights[0].value = -0.1;
    if (device_a && frame == 9) {
        vertices[4].weights = {
            {20, 0.25}, {21, 0.25}, {22, 0.2}, {23, 0.2}, {24, 0.1},
        };
    }
    if (!device_a && frame == 15) vertices[4].weights[1].bone_id = vertices[4].weights[0].bone_id;
    return vertices;
}

}  // namespace

const char* resource_mode_name(ResourceMode mode) {
    return mode == ResourceMode::Full ? "full" : "cpu_streaming_degraded";
}

Capture make_demo_capture() {
    Capture capture;
    capture.asset_id = "two-device-robot-arm";
    capture.frame_period_us = 10000;

    DeviceCapture device_a;
    device_a.device_id = "device-a";
    device_a.clock = {500, 0.0, 50};
    DeviceCapture device_b;
    device_b.device_id = "device-b";
    device_b.clock = {38000, 4000.0, 2500};

    for (int frame = 0; frame < 24; ++frame) {
        DeviceFrame a;
        a.sequence = frame;
        a.local_time_us = device_a.clock.offset_us + frame * capture.frame_period_us;
        a.vertices = make_vertices(true, frame);
        device_a.frames.push_back(std::move(a));

        if (frame >= 8 && frame <= 11) continue;
        DeviceFrame b;
        b.sequence = frame;
        b.local_time_us = static_cast<std::int64_t>(std::llround(
            static_cast<double>(device_b.clock.offset_us) +
            static_cast<double>(frame * capture.frame_period_us) *
                (1.0 + device_b.clock.drift_ppm / 1000000.0)));
        b.vertices = make_vertices(false, frame);
        device_b.frames.push_back(std::move(b));
    }

    capture.devices.push_back(std::move(device_a));
    capture.devices.push_back(std::move(device_b));
    return capture;
}

Timeline build_timeline(const Capture& capture, int min_gap_frames) {
    if (capture.devices.empty()) throw std::runtime_error("capture has no devices");
    if (capture.frame_period_us <= 0) throw std::runtime_error("capture frame period is invalid");

    struct DeviceSlots {
        const DeviceCapture* device = nullptr;
        ClockProjection projection;
        std::map<std::int64_t, const DeviceFrame*> frames;
    };

    Timeline timeline;
    timeline.frame_period_us = capture.frame_period_us;
    std::vector<DeviceSlots> devices;
    std::int64_t min_slot = 0;
    std::int64_t max_slot = 0;
    bool have_slot = false;

    for (const DeviceCapture& device : capture.devices) {
        DeviceSlots projected;
        projected.device = &device;
        projected.projection.device_id = device.device_id;
        projected.projection.offset_us = device.clock.offset_us;
        projected.projection.drift_ppm = device.clock.drift_ppm;
        projected.projection.uncertainty_us = device.clock.uncertainty_us;
        bool have_reference = false;

        for (const DeviceFrame& frame : device.frames) {
            const std::int64_t reference_us = project_time(device.clock, frame.local_time_us);
            const std::int64_t slot = reference_to_slot(reference_us, capture.frame_period_us);
            if (projected.frames.count(slot) != 0) {
                throw std::runtime_error("two frames from " + device.device_id +
                                         " project to reference frame " + std::to_string(slot));
            }
            projected.frames.emplace(slot, &frame);
            if (!have_reference) {
                projected.projection.first_reference_us = reference_us;
                projected.projection.last_reference_us = reference_us;
                have_reference = true;
            } else {
                projected.projection.first_reference_us =
                    std::min(projected.projection.first_reference_us, reference_us);
                projected.projection.last_reference_us =
                    std::max(projected.projection.last_reference_us, reference_us);
            }
            if (!have_slot) {
                min_slot = max_slot = slot;
                have_slot = true;
            } else {
                min_slot = std::min(min_slot, slot);
                max_slot = std::max(max_slot, slot);
            }
        }
        timeline.clocks.push_back(projected.projection);
        devices.push_back(std::move(projected));
    }
    if (!have_slot) throw std::runtime_error("capture has no frames");

    timeline.reference_origin_us = min_slot * capture.frame_period_us;
    for (std::int64_t slot = min_slot; slot <= max_slot; ++slot) {
        TimelineSlot result;
        result.frame = static_cast<int>(slot - min_slot);
        result.reference_us = slot * capture.frame_period_us;
        for (const DeviceSlots& device : devices) {
            const auto found = device.frames.find(slot);
            if (found == device.frames.end()) {
                result.missing_devices.push_back(device.device->device_id);
            } else {
                result.device_frames.push_back(found->second);
                result.present_devices.push_back(device.device->device_id);
            }
        }
        timeline.slots.push_back(std::move(result));
    }

    for (const DeviceSlots& device : devices) {
        if (device.frames.size() < 2) continue;
        auto previous = device.frames.begin();
        for (auto current = std::next(previous); current != device.frames.end(); ++current) {
            const std::int64_t slot_delta = current->first - previous->first;
            const std::int64_t missing_count = slot_delta - 1;
            if (missing_count >= min_gap_frames) {
                MissingSegment segment;
                segment.device_id = device.device->device_id;
                segment.first_frame = static_cast<int>(previous->first + 1 - min_slot);
                segment.last_frame = static_cast<int>(current->first - 1 - min_slot);
                segment.reference_start_us = (previous->first + 1) * capture.frame_period_us;
                segment.reference_end_us = current->first * capture.frame_period_us;
                segment.observed_local_gap_us =
                    current->second->local_time_us - previous->second->local_time_us;
                segment.nominal_local_gap_us = slot_delta * capture.frame_period_us;
                segment.expected_local_gap_us = static_cast<std::int64_t>(std::llround(
                    static_cast<double>(slot_delta * capture.frame_period_us) *
                    (1.0 + device.device->clock.drift_ppm / 1000000.0)));
                segment.clock_skew_effect_us =
                    segment.observed_local_gap_us - segment.nominal_local_gap_us;
                segment.boundary_uncertainty_us = device.device->clock.uncertainty_us * 2;
                segment.reason = "unsynchronized_clock_projection_gap";
                timeline.missing_segments.push_back(std::move(segment));
            }
            previous = current;
        }
    }
    timeline.slot_count = static_cast<int>(timeline.slots.size());
    return timeline;
}

ResourcePlan plan_resources(const Capture& capture, const Timeline& timeline,
                            const ResourcePolicy& policy) {
    std::int64_t vertices = 0;
    std::int64_t influences = 0;
    for (const DeviceCapture& device : capture.devices) {
        for (const DeviceFrame& frame : device.frames) {
            vertices += static_cast<std::int64_t>(frame.vertices.size());
            for (const Vertex& vertex : frame.vertices) {
                influences += static_cast<std::int64_t>(vertex.weights.size());
            }
        }
    }

    ResourcePlan plan;
    plan.memory_budget_bytes = policy.memory_budget_bytes;
    plan.max_influences = policy.max_influences;
    plan.max_bone_id = policy.max_bone_id;
    plan.weight_epsilon = policy.weight_epsilon;
    plan.estimated_full_bytes =
        vertices * 64 + influences * 48 +
        static_cast<std::int64_t>(timeline.slots.size()) * 256 +
        static_cast<std::int64_t>(timeline.missing_segments.size()) * 128 +
        64 * 1024;

    if (plan.estimated_full_bytes <= policy.memory_budget_bytes) {
        plan.mode = ResourceMode::Full;
        plan.reason = "memory_budget_sufficient_for_retained_evidence";
        return plan;
    }
    if (!policy.allow_degraded) {
        throw std::runtime_error("insufficient memory budget and degraded mode is disabled");
    }
    plan.mode = ResourceMode::Degraded;
    plan.skipped_checks = {"duplicate_bone"};
    plan.reason = "memory_budget_below_full_estimate";
    return plan;
}

FrameResult check_frame(const Timeline& timeline, int frame, const ResourcePlan& plan) {
    if (frame < 0 || frame >= static_cast<int>(timeline.slots.size())) {
        throw std::runtime_error("frame index out of range: " + std::to_string(frame));
    }
    const TimelineSlot& slot = timeline.slots[static_cast<std::size_t>(frame)];
    FrameResult result;
    result.frame = frame;
    result.reference_us = slot.reference_us;
    result.present_devices = slot.present_devices;
    result.missing_devices = slot.missing_devices;
    result.input_sha256 = frame_input_digest(slot);

    struct Entry {
        std::string device_id;
        const Vertex* vertex = nullptr;
        std::size_t device_order = 0;
    };
    std::vector<Entry> entries;
    for (std::size_t device_index = 0; device_index < slot.device_frames.size(); ++device_index) {
        const DeviceFrame* device_frame = slot.device_frames[device_index];
        for (const Vertex& vertex : device_frame->vertices) {
            entries.push_back({slot.present_devices[device_index], &vertex, device_index});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        if (left.vertex->vertex_id != right.vertex->vertex_id) {
            return left.vertex->vertex_id < right.vertex->vertex_id;
        }
        return left.device_order < right.device_order;
    });

    for (const Entry& entry : entries) {
        const Vertex& vertex = *entry.vertex;
        ++result.vertices_checked;
        result.influences_checked += static_cast<int>(vertex.weights.size());
        const std::string& device = entry.device_id;
        const std::int64_t vertex_id = static_cast<std::int64_t>(vertex.vertex_id);
        if (vertex.weights.empty()) {
            add_issue(result, "no_influences", device, vertex_id, -1, 0.0, 1.0,
                      "vertex has no skin influences");
            continue;
        }
        if (vertex.weights.size() > plan.max_influences) {
            add_issue(result, "too_many_influences", device, vertex_id, -1,
                      static_cast<double>(vertex.weights.size()),
                      static_cast<double>(plan.max_influences),
                      "vertex exceeds the influence limit");
        }

        double sum = 0.0;
        bool finite = true;
        std::unordered_set<std::uint32_t> bones;
        for (std::size_t influence_index = 0; influence_index < vertex.weights.size();
             ++influence_index) {
            const Weight& weight = vertex.weights[influence_index];
            if (!std::isfinite(weight.value)) {
                finite = false;
                add_issue(result, "non_finite_weight", device, vertex_id,
                          static_cast<std::int64_t>(weight.bone_id), weight.value, 0.0,
                          "weight is NaN or infinity");
            } else {
                sum += weight.value;
                if (weight.value < 0.0) {
                    add_issue(result, "negative_weight", device, vertex_id,
                              static_cast<std::int64_t>(weight.bone_id), weight.value, 0.0,
                              "weight must be non-negative");
                }
            }
            if (weight.bone_id > plan.max_bone_id) {
                add_issue(result, "bone_index_out_of_range", device, vertex_id,
                          static_cast<std::int64_t>(weight.bone_id),
                          static_cast<double>(weight.bone_id),
                          static_cast<double>(plan.max_bone_id),
                          "bone index exceeds capture limit");
            }
            if (plan.mode == ResourceMode::Full && !bones.insert(weight.bone_id).second) {
                add_issue(result, "duplicate_bone", device, vertex_id,
                          static_cast<std::int64_t>(weight.bone_id),
                          static_cast<double>(weight.bone_id), 1.0,
                          "bone appears more than once for one vertex");
            }
        }
        if (finite && std::abs(sum - 1.0) > plan.weight_epsilon) {
            add_issue(result, "weight_sum_mismatch", device, vertex_id, -1, sum, 1.0,
                      "skin weights must sum to one within tolerance");
        }
    }

    const bool failed = !result.issues.empty();
    const bool incomplete = !result.missing_devices.empty();
    const bool degraded = plan.mode == ResourceMode::Degraded;
    if (failed && incomplete) result.status = "fail_incomplete";
    else if (failed) result.status = degraded ? "fail_degraded" : "fail";
    else if (incomplete) result.status = "incomplete";
    else if (degraded) result.status = "pass_degraded";
    else result.status = "pass";
    result.result_sha256 = frame_result_digest(result);
    return result;
}

std::vector<FrameResult> run_frame_range(const Timeline& timeline, int first_frame,
                                         int last_frame, const ResourcePlan& plan) {
    std::vector<FrameResult> results;
    first_frame = std::max(first_frame, 0);
    last_frame = std::min(last_frame, static_cast<int>(timeline.slots.size()));
    for (int frame = first_frame; frame < last_frame; ++frame) {
        results.push_back(check_frame(timeline, frame, plan));
    }
    return results;
}

Summary summarize(const std::vector<FrameResult>& frames) {
    Summary summary;
    summary.frames = static_cast<int>(frames.size());
    for (const FrameResult& frame : frames) {
        summary.issues += static_cast<int>(frame.issues.size());
        if (frame.status == "pass") {
            ++summary.pass;
        } else if (frame.status == "pass_degraded") {
            ++summary.degraded;
        } else if (frame.status == "fail" || frame.status == "fail_degraded" ||
                   frame.status == "fail_incomplete") {
            ++summary.fail;
        } else if (frame.status == "incomplete") {
            ++summary.incomplete;
        }
    }
    if (summary.frames == 0) {
        summary.status = "empty";
    } else if (summary.fail > 0) {
        summary.status = "fail";
    } else if (summary.incomplete > 0) {
        summary.status = "incomplete";
    } else if (summary.degraded > 0) {
        summary.status = "pass_degraded";
    } else {
        summary.status = "pass";
    }
    return summary;
}

ResumeComparison compare_resume_results(const Timeline& timeline, const ResourcePlan& plan,
                                        const std::vector<FrameResult>& checkpoint_frames,
                                        const std::vector<FrameResult>& final_frames) {
    ResumeComparison comparison;
    comparison.frames_rechecked = static_cast<int>(checkpoint_frames.size());
    comparison.new_frames = static_cast<int>(final_frames.size() - checkpoint_frames.size());
    comparison.before_status = summarize(checkpoint_frames).status;
    comparison.after_status = summarize(final_frames).status;
    comparison.stable = true;

    for (const FrameResult& before : checkpoint_frames) {
        const FrameResult recomputed = check_frame(timeline, before.frame, plan);
        FrameComparison item;
        item.frame = before.frame;
        item.checkpoint_result_sha256 = before.result_sha256;
        item.recomputed_result_sha256 = recomputed.result_sha256;
        item.matched = before.result_sha256 == recomputed.result_sha256;
        if (item.matched) {
            ++comparison.matched;
        } else {
            ++comparison.mismatched;
            comparison.stable = false;
        }
        comparison.frames.push_back(std::move(item));
    }
    return comparison;
}

}  // namespace skincheck
