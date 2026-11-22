#pragma once

#include "clock_model.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace ph {

class VulkanRenderer;

struct AppConfig {
    ClockConfig clock;
    int64_t memory_budget_bytes = 16 * 1024 * 1024;
    std::string max_quality = "high";
    std::string output_dir = "artifacts/demo";
    std::string mode = "both";
    std::string input_path;
    int pause_after = 5;
    bool json_stdout = false;
};

struct QualityProfile {
    std::string name;
    int width = 0;
    int height = 0;
    int retained_frames = 0;
};

struct DegradationStep {
    std::string from_quality;
    std::string to_quality;
    std::string reason;
    int64_t estimated_bytes = 0;
    int64_t budget_bytes = 0;
    std::string effect;
};

struct ResourcePlan {
    std::string requested_quality;
    std::string max_quality;
    std::string effective_quality;
    std::string status;
    std::string reason;
    int width = 0;
    int height = 0;
    int retained_frames = 0;
    int64_t budget_bytes = 0;
    int64_t estimated_bytes = 0;
    std::vector<DegradationStep> degradations;
    std::vector<std::string> allocation_attempts;
    bool blocked = false;
};

struct ProbeSample {
    std::string probe;
    int x = 0;
    int y = 0;
    int rgba[4] = {0, 0, 0, 0};
    int object_id = 0;
    std::string object_name;
    int model_rgba[4] = {0, 0, 0, 0};
};

struct FrameRecord {
    int step = 0;
    std::string device;
    int64_t raw_clock_us = 0;
    int64_t truth_tick_us = 0;
    int64_t scene_tick_us = 0;
    int64_t calibrated_tick_us = 0;
    int64_t clock_delta_vs_a_us = 0;
    std::string frame_digest;
    std::vector<ProbeSample> probes;
    std::vector<uint8_t> cached_rgba;
};

struct DeviceStream {
    std::string id;
    int device_index = 0;
    int64_t epoch_us = 0;
    std::vector<FrameRecord> frames;
};

struct RunSummary {
    int frames = 0;
    int probe_samples = 0;
    int object_hits = 0;
    std::string sample_digest;
};

struct ResumeEvidence {
    bool loaded = false;
    bool artifact_digest_valid = false;
    bool checkpoint_roundtrip_byte_stable = false;
    bool checkpoint_field_order_preserved = false;
    int checkpoint_step = -1;
    int compared_pixels = 0;
    int mismatches = 0;
    bool frame_digest_match = false;
    bool match = false;
    std::string checkpoint_digest;
    std::string rerender_digest_device_a;
    std::string rerender_digest_device_b;
};

struct ContinuousComparison {
    bool evaluated = false;
    bool match = false;
    std::string reference_path;
    std::string reference_digest;
    std::string current_digest;
};

struct RunResult {
    std::string schema = "pixel-history.run";
    int schema_version = 1;
    std::string run_id;
    std::string mode;
    std::string status;
    std::string runtime_device;
    std::string runtime_api_version;
    ClockConfig clock;
    ResourcePlan plan;
    std::vector<DeviceStream> devices;
    int frame_count = 0;
    int next_step = 0;
    int pause_after = 0;
    int resume_start_step = -1;
    std::string checkpoint_path;
    bool paused = false;
    RunSummary pre_summary;
    RunSummary post_summary;
    ResumeEvidence resume;
    ContinuousComparison comparison;
    std::vector<std::string> notes;
};

struct LoadedCheckpoint {
    RunResult prefix;
    int next_step = 0;
    std::string raw_text;
    std::string artifact_digest;
    bool roundtrip_byte_stable = false;
    bool field_order_preserved = false;
    bool digest_valid = false;
};

const std::vector<QualityProfile>& qualityProfiles();
ResourcePlan chooseResourcePlan(const AppConfig& config);
bool degradeResourcePlan(ResourcePlan& plan, const AppConfig& config, const std::string& reason);

RunResult makeRunResult(const AppConfig& config, const ResourcePlan& plan);
bool processFrames(const AppConfig& config, RunResult& result, VulkanRenderer& renderer,
                   int start_step, int end_step, std::string& error);

RunSummary summarize(const RunResult& result, int first_step, int limit_step);
std::string streamDigest(const RunResult& result);
std::string frameDigest(const FrameRecord& frame);
FrameRecord renderAndSampleFrame(const AppConfig& config, const ResourcePlan& plan,
                                 VulkanRenderer& renderer, int device_index, int step,
                                 std::string& error);
bool verifyResumeFrame(const AppConfig& config, const ResourcePlan& plan,
                       VulkanRenderer& renderer, const RunResult& prefix,
                       ResumeEvidence& evidence, std::string& error);

}  // namespace ph
