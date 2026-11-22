#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "config.hpp"
#include "evidence.hpp"

namespace grid {

struct DrawCallRecord {
    uint32_t index;            // 帧内序号
    std::string name;          // 语义名称
    uint32_t vertexCount;
    uint32_t instanceCount;
    VkPrimitiveTopology topology;
};

struct BarrierRecord {
    uint32_t sequence;         // 命令缓冲内序号
    std::string image;
    std::string fromLayout;
    std::string toLayout;
    VkPipelineStageFlags srcStage;
    VkPipelineStageFlags dstStage;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // GLFW 初始化（headless 时不创建窗口），随后建立 Vulkan 设备与离屏资源
    bool initialize(const Config& config, EvidenceStore* evidence, std::string* error);

    // 渲染一帧并读回 RGBA8；draw call/屏障记录写入 evidence
    bool renderFrame(uint32_t frameIndex, std::vector<uint8_t>* pixels,
                     FrameEvidence* frameEvidence, std::string* error);

    void shutdown();

    const std::vector<DrawCallRecord>& lastDrawCalls() const { return lastDrawCalls_; }
    const std::vector<BarrierRecord>& lastBarriers() const { return lastBarriers_; }
    std::string deviceName() const { return deviceName_; }
    uint32_t apiVersion() const { return apiVersion_; }

private:
    bool initGlfw(const Config& config, std::string* error);
    bool createInstance(const Config& config, std::string* error);
    bool pickPhysicalDevice(const Config& config, std::string* error);
    bool createLogicalDevice(std::string* error);
    bool createOffscreenTargets(const Config& config, std::string* error);
    bool createRenderPassAndPipeline(const Config& config, std::string* error);
    bool createCommandObjects(std::string* error);
    bool readPixels(std::vector<uint8_t>* pixels, std::string* error);
    void recordBarrier(const char* imageName, VkImageLayout from, VkImageLayout to,
                       VkPipelineStageFlags src, VkPipelineStageFlags dst);

    const Config* config_ = nullptr;
    EvidenceStore* evidence_ = nullptr;

    bool glfwInitialized_ = false;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkImage colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
    VkImageView colorView_ = VK_NULL_HANDLE;
    VkImage hostImage_ = VK_NULL_HANDLE;
    VkDeviceMemory hostMemory_ = VK_NULL_HANDLE;
    VkDeviceSize hostImageSize_ = 0;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline linePipeline_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkImageLayout colorLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    std::string deviceName_;
    uint32_t apiVersion_ = 0;
    bool validationEnabled_ = false;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;

    std::vector<DrawCallRecord> lastDrawCalls_;
    std::vector<BarrierRecord> lastBarriers_;
};

}  // namespace grid
