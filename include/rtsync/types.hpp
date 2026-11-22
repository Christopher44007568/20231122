#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rtsync {

constexpr const char* kTraceSchema = "rtsync.trace.v1";
constexpr const char* kAnalysisSchema = "rtsync.analysis.v1";
constexpr const char* kVersion = "1.0.0";

constexpr std::uint64_t kDefaultMaxInputBytes = 32ull * 1024ull * 1024ull;
constexpr std::uint64_t kDefaultMaxRecords = 100000;
constexpr std::size_t kDefaultMaxFindings = 256;
constexpr std::size_t kEvidencePrefixBytes = 64ull * 1024ull;

struct Limits {
  std::uint64_t max_input_bytes = kDefaultMaxInputBytes;
  std::uint64_t max_records = kDefaultMaxRecords;
  std::size_t max_findings = kDefaultMaxFindings;
};

struct Capture {
  std::string id;
  std::string scenario;
  std::string started_utc;
  std::string description;
};

struct ClockPoint {
  std::int64_t host_ns = 0;
  std::int64_t device_ticks = 0;
};

struct Device {
  std::string id;
  std::string name;
  std::uint64_t clock_frequency_hz = 1000000000ull;
  std::vector<ClockPoint> calibration;
};

struct ThreadEvent {
  std::string kind;
  std::string target;
  std::string state;
  std::int64_t host_ns = 0;
  std::int64_t device_ticks = 0;
};

struct RenderThread {
  std::string id;
  std::string device_id;
  std::uint64_t os_thread_id = 0;
  std::vector<ThreadEvent> events;
};

struct CommandOp {
  std::uint32_t seq = 0;
  std::string op;
  std::string target;
  std::string draw_id;
  std::string resource_id;
  std::string from_state;
  std::string to_state;
  std::int64_t host_ns = 0;
  std::int64_t device_ticks = 0;
};

struct CommandBuffer {
  std::string id;
  std::string device_id;
  std::string thread_id;
  std::string fence_id;
  std::string state;
  std::vector<CommandOp> operations;
};

struct FenceTime {
  bool present = false;
  std::int64_t host_ns = 0;
  std::int64_t device_ticks = 0;
};

struct Fence {
  std::string id;
  std::string device_id;
  std::string command_buffer_id;
  std::string state;
  FenceTime signal;
  FenceTime wait;
};

struct DrawCall {
  std::string id;
  std::string device_id;
  std::string thread_id;
  std::string command_buffer_id;
  std::string framebuffer_id;
  std::string resource_state;
  std::vector<std::string> resource_ids;
  std::int64_t start_device_ticks = 0;
  std::int64_t end_device_ticks = 0;
};

struct ResourceAccess {
  std::string device_id;
  std::string thread_id;
  std::string draw_id;
  std::string mode;
  std::string state;
  std::int64_t start_device_ticks = 0;
  std::int64_t end_device_ticks = 0;
};

struct Resource {
  std::string id;
  std::string device_id;
  std::string kind;
  std::string initial_state;
  std::vector<ResourceAccess> accesses;
};

struct Framebuffer {
  std::string id;
  std::string device_id;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::string pixel_file;
  std::string sha256;
};

struct Frame {
  std::string id;
  std::string group;
  std::string role;
  std::string device_id;
  std::string draw_id;
  std::string framebuffer_id;
  std::string image_file;
  std::int64_t host_ns = 0;
  std::int64_t device_ticks = 0;
};

struct Trace {
  std::string schema = kTraceSchema;
  Capture capture;
  std::vector<Device> devices;
  std::vector<RenderThread> render_threads;
  std::vector<CommandBuffer> command_buffers;
  std::vector<Fence> fences;
  std::vector<DrawCall> draw_calls;
  std::vector<Resource> resources;
  std::vector<Framebuffer> framebuffers;
  std::vector<Frame> frames;
  std::vector<std::string> warnings;
  std::uint64_t record_count = 0;
};

struct ClockModel {
  std::string device_id;
  bool calibrated = false;
  std::string method;
  long double slope_ns_per_tick = 0.0L;
  long double intercept_ns = 0.0L;
  long double max_residual_ns = 0.0L;
  std::size_t calibration_points = 0;
  bool valid = false;
  std::string error;
};

struct TimeSpan {
  std::int64_t raw_start = 0;
  std::int64_t raw_end = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

struct VisualDiff {
  std::string id;
  std::string group;
  std::string golden_frame_id;
  std::string observed_frame_id;
  std::string golden_file;
  std::string observed_file;
  std::string diff_file;
  std::string golden_sha256;
  std::string observed_sha256;
  std::string diff_sha256;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t changed_pixels = 0;
  std::uint64_t total_pixels = 0;
  std::uint64_t max_channel_delta = 0;
  double mean_abs_error = 0.0;
  bool bbox_present = false;
  std::uint32_t bbox_x0 = 0;
  std::uint32_t bbox_y0 = 0;
  std::uint32_t bbox_x1 = 0;
  std::uint32_t bbox_y1 = 0;
  bool image_error = false;
  std::string error;
};

struct Finding {
  std::string id;
  std::string type;
  std::string severity;
  std::string message;
  std::vector<std::string> draw_ids;
  std::vector<std::string> resource_ids;
  std::vector<std::string> fence_ids;
  std::vector<std::string> command_buffer_ids;
  std::vector<std::string> frame_ids;
  std::string resource_state;
  std::string raw_clock_evidence;
  std::string normalized_clock_evidence;
};

struct Correlation {
  std::string id;
  std::string finding_id;
  std::vector<std::string> draw_ids;
  std::vector<std::string> resource_ids;
  std::string resource_state;
  std::vector<std::string> fence_ids;
  std::vector<std::string> command_buffer_ids;
  std::vector<std::string> framebuffer_ids;
  std::vector<std::string> frame_ids;
  std::vector<std::string> diff_ids;
};

struct Analysis {
  Trace trace;
  std::vector<ClockModel> clocks;
  std::vector<VisualDiff> diffs;
  std::vector<Finding> findings;
  std::vector<Correlation> correlations;
  std::string status = "ok";
  std::string status_detail;
  std::string input_sha256;
  std::uint64_t input_bytes = 0;
  std::uint64_t observed_records = 0;
  Limits limits;
  bool empty = false;
};

}  // namespace rtsync

