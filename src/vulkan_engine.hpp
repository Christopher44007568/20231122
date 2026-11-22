#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "capture.hpp"

namespace inspect {

struct EngineOptions {
    std::string requested_mode = "auto";
    std::string shader_dir;
    std::uint32_t requested_width = 512;
    std::uint32_t requested_height = 512;
    std::uint64_t memory_budget_bytes = 0;
    bool force_low_memory = false;
    bool allow_cpu_fallback = true;
    bool verbose = false;
};

struct ResourcePlan {
    std::string backend = "cpu";
    std::string requested_mode = "auto";
    std::string selected_mode = "cpu";
    std::string device_name = "unavailable";
    std::string driver_name;
    std::string api_version;
    std::vector<std::string> reasons;
    bool degraded = true;
    bool geometry_shader_supported = false;
    bool vertex_pipeline_stores_supported = false;
    bool cpu_fallback_allowed = true;
    std::uint64_t heap_bytes = 0;
    std::uint64_t memory_budget_bytes = 0;
    std::uint64_t required_bytes = 0;
    std::uint64_t input_bytes = 0;
    std::uint64_t capture_bytes = 0;
    std::uint64_t image_bytes = 0;
    std::uint32_t capture_records = 0;
    std::uint32_t max_storage_buffer_range = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    std::string toJson() const;
};

class VulkanEngine {
public:
    VulkanEngine();
    ~VulkanEngine();
    VulkanEngine(const VulkanEngine&) = delete;
    VulkanEngine& operator=(const VulkanEngine&) = delete;

    bool initialize(const EngineOptions& options, const Material& material,
                    ResourcePlan& plan, std::string& error);
    bool geometryPass(const Material& material,
                      std::vector<std::uint8_t>& capture_bytes,
                      std::string& error);
    bool compositePass(const std::vector<std::uint8_t>& capture_bytes,
                       std::vector<std::uint8_t>& rgba, std::string& error);
    void shutdown();

    const ResourcePlan& plan() const { return plan_; }
    bool usesVulkan() const { return plan_.backend == "vulkan"; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ResourcePlan plan_;
};

ResourcePlan makeCpuFallbackPlan(const EngineOptions& options,
                                 const Material& material,
                                 const std::string& reason);

}  // namespace inspect
