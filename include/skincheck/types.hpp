#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "skincheck/json.hpp"

namespace skincheck {

struct Weight {
    std::uint32_t bone_id = 0;
    double value = 0.0;
};

struct Vertex {
    std::uint32_t vertex_id = 0;
    std::vector<Weight> weights;
};

struct DeviceFrame {
    std::int64_t sequence = 0;
    std::int64_t local_time_us = 0;
    std::vector<Vertex> vertices;
};

struct DeviceClock {
    std::int64_t offset_us = 0;
    double drift_ppm = 0.0;
    std::int64_t uncertainty_us = 0;
};

struct DeviceCapture {
    std::string device_id;
    DeviceClock clock;
    std::vector<DeviceFrame> frames;
};

struct Capture {
    std::string schema = "skincheck.capture/v1";
    std::string asset_id;
    std::int64_t frame_period_us = 10000;
    std::vector<DeviceCapture> devices;
};

struct ClockProjection {
    std::string device_id;
    std::int64_t offset_us = 0;
    double drift_ppm = 0.0;
    std::int64_t uncertainty_us = 0;
    std::int64_t first_reference_us = 0;
    std::int64_t last_reference_us = 0;
};

struct MissingSegment {
    std::string device_id;
    int first_frame = 0;
    int last_frame = 0;
    std::int64_t reference_start_us = 0;
    std::int64_t reference_end_us = 0;
    std::int64_t observed_local_gap_us = 0;
    std::int64_t nominal_local_gap_us = 0;
    std::int64_t expected_local_gap_us = 0;
    std::int64_t clock_skew_effect_us = 0;
    std::int64_t boundary_uncertainty_us = 0;
    std::string reason;
};

struct TimelineSlot {
    int frame = 0;
    std::int64_t reference_us = 0;
    std::vector<const DeviceFrame*> device_frames;
    std::vector<std::string> present_devices;
    std::vector<std::string> missing_devices;
};

struct Timeline {
    int slot_count = 0;
    std::int64_t reference_origin_us = 0;
    std::int64_t frame_period_us = 0;
    std::vector<ClockProjection> clocks;
    std::vector<MissingSegment> missing_segments;
    std::vector<TimelineSlot> slots;
};

struct ResourcePolicy {
    std::int64_t memory_budget_bytes = 16 * 1024 * 1024;
    std::uint32_t max_influences = 4;
    std::uint32_t max_bone_id = 127;
    double weight_epsilon = 0.001;
    int min_gap_frames = 3;
    bool allow_degraded = true;
};

enum class ResourceMode { Full, Degraded };

struct ResourcePlan {
    ResourceMode mode = ResourceMode::Full;
    std::int64_t memory_budget_bytes = 0;
    std::int64_t estimated_full_bytes = 0;
    std::uint32_t max_influences = 4;
    std::uint32_t max_bone_id = 127;
    double weight_epsilon = 0.001;
    std::vector<std::string> skipped_checks;
    std::string reason;
};

struct Issue {
    std::string code;
    std::string severity = "error";
    int frame = 0;
    std::string device_id;
    std::int64_t vertex_id = -1;
    std::int64_t bone_id = -1;
    double actual = 0.0;
    double limit = 0.0;
    std::string detail;
};

struct FrameResult {
    int frame = 0;
    std::int64_t reference_us = 0;
    std::vector<std::string> present_devices;
    std::vector<std::string> missing_devices;
    int vertices_checked = 0;
    int influences_checked = 0;
    std::vector<Issue> issues;
    std::string status;
    std::string input_sha256;
    std::string result_sha256;
};

struct Summary {
    int frames = 0;
    int pass = 0;
    int fail = 0;
    int incomplete = 0;
    int degraded = 0;
    int issues = 0;
    std::string status = "empty";
};

struct Checkpoint {
    std::string schema = "skincheck.checkpoint/v1";
    std::string run_id;
    std::string phase = "paused";
    std::string pause_reason;
    std::string capture_sha256;
    Capture capture;
    ResourcePolicy policy;
    ResourcePlan resources;
    Timeline timeline;
    int next_frame = 0;
    std::vector<FrameResult> completed_frames;
    Summary summary;
};

struct FrameComparison {
    int frame = 0;
    bool matched = false;
    std::string checkpoint_result_sha256;
    std::string recomputed_result_sha256;
};

struct ResumeComparison {
    int frames_rechecked = 0;
    int matched = 0;
    int mismatched = 0;
    int new_frames = 0;
    bool stable = false;
    std::string before_status;
    std::string after_status;
    std::vector<FrameComparison> frames;
};

Summary summarize(const std::vector<FrameResult>& frames);
const char* resource_mode_name(ResourceMode mode);

}  // namespace skincheck
