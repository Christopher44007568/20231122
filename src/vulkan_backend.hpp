#pragma once

#include "vegetation.hpp"

#include <string>

namespace vegd {

class VulkanBackend {
public:
    VulkanBackend() = default;
    ~VulkanBackend();
    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;

    bool initialize(std::string& error);
    bool generateTile(const Params& params, uint32_t tile, uint32_t capacity,
                      TileData& result, std::string& error);
    bool available() const { return initialized_; }
    const std::string& deviceName() const { return deviceName_; }

private:
    void shutdown();
    bool loadShader(std::string& error);
    bool ensureCapacity(uint32_t capacity, std::string& error);
    bool createPipeline(std::string& error);

    bool initialized_ = false;
    std::string deviceName_;

    void* instance_ = nullptr;
    void* physicalDevice_ = nullptr;
    void* device_ = nullptr;
    void* computeQueue_ = nullptr;
    void* commandPool_ = nullptr;
    void* commandBuffer_ = nullptr;
    void* fence_ = nullptr;
    void* shaderModule_ = nullptr;
    void* descriptorSetLayout_ = nullptr;
    void* descriptorPool_ = nullptr;
    void* descriptorSet_ = nullptr;
    void* pipelineLayout_ = nullptr;
    void* pipeline_ = nullptr;
    void* outputBuffer_ = nullptr;
    void* outputMemory_ = nullptr;
    void* mappedOutput_ = nullptr;

    unsigned long long outputBufferSize_ = 0;
    unsigned int outputCapacity_ = 0;
};

}  // namespace vegd
