#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gpu {

struct FrameSample {
    uint64_t ts_begin = 0;
    uint64_t ts_end = 0;
    // Bit 0: begin timestamp available. Bit 1: end timestamp available.
    uint32_t availability = 0;
};

struct DeviceInfo {
    std::string name;
    uint32_t api_version = 0;
    float timestamp_period_ns = 1.0f;
    uint32_t timestamp_valid_bits = 0;
    uint32_t queue_family = 0;
    bool glfw_null_platform = false;
};

class GpuContext {
  public:
    // Initializes GLFW (hidden NO_API window) and a Vulkan instance/device
    // with a timestamp-capable queue. Returns false with `error` set on
    // failure.
    bool init(std::string& error);
    void shutdown();

    // Runs one frame of GPU work bracketed by timestamp queries.
    // If skip_end_timestamp is true the end query is never written, which
    // makes its availability bit read back as 0 (a genuinely unavailable
    // GPU timestamp, used to model an undecidable boundary).
    bool sample_frame(bool skip_end_timestamp, FrameSample& out, std::string& error);

    const DeviceInfo& device() const { return device_; }

  private:
    struct Impl;
    Impl* impl_ = nullptr;
    DeviceInfo device_;
};

}  // namespace gpu
