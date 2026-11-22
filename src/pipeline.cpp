#include "pipeline.hpp"

#include "vulkan_backend.hpp"

#include <memory>

namespace vegd {

TileProcessor::TileProcessor(Params params, ResourcePlan plan, bool forceCpu)
    : params_(params), plan_(std::move(plan)), forceCpu_(forceCpu) {
    degradations_ = plan_.degradations;
}

TileProcessor::~TileProcessor() {
    delete static_cast<VulkanBackend*>(vulkan_);
}

bool TileProcessor::initialize(std::string& error) {
    if (forceCpu_) {
        fallbackToCpu("cpu-forced");
        initialized_ = true;
        return true;
    }
    auto backend = std::make_unique<VulkanBackend>();
    std::string vulkanError;
    if (!backend->initialize(vulkanError)) {
        fallbackToCpu("vulkan-unavailable: " + vulkanError);
        initialized_ = true;
        error.clear();
        return true;
    }
    deviceName_ = backend->deviceName();
    backendName_ = "vulkan";
    usingVulkan_ = true;
    vulkan_ = backend.release();
    initialized_ = true;
    return true;
}

bool TileProcessor::fallbackToCpu(const std::string& reason) {
    if (usingVulkan_) {
        delete static_cast<VulkanBackend*>(vulkan_);
        vulkan_ = nullptr;
        usingVulkan_ = false;
    }
    backendName_ = "cpu";
    deviceName_ = "deterministic-cpu";
    degradations_.push_back(reason + ";cpu-fallback");
    return true;
}

bool TileProcessor::generate(uint32_t tile, TileData& result,
                             std::string& error) {
    if (!initialized_) {
        error = "tile processor is not initialized";
        return false;
    }
    if (isMissingTile(params_, tile)) {
        result = generateTileCpu(params_, tile, capacity());
        return true;
    }
    if (usingVulkan_) {
        auto* backend = static_cast<VulkanBackend*>(vulkan_);
        std::string tileError;
        if (backend->generateTile(params_, tile, capacity(), result, tileError)) {
            return true;
        }
        fallbackToCpu("vulkan-tile-failed: " + tileError);
    }
    result = generateTileCpu(params_, tile, capacity());
    return true;
}

}  // namespace vegd
