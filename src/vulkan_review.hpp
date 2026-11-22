#pragma once

#include "cache_format.hpp"
#include "image.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lshc {

struct DrawCallEvidence {
    uint64_t commandBuffer = 0;
    uint64_t pipeline = 0;
    uint64_t pipelineLayout = 0;
    uint64_t descriptorSet = 0;
    uint32_t ordinal = 1;
    uint32_t vertexCount = 3;
    uint32_t instanceCount = 1;
    uint32_t firstVertex = 0;
    uint32_t firstInstance = 0;
    std::string topology;
    std::string descriptorBinding;
};

struct ResourceStateEvidence {
    uint64_t handle = 0;
    uint64_t memoryHandle = 0;
    uint64_t memoryBytes = 0;
    uint64_t contentCrc32c = 0;
    std::string kind;
    std::string format;
    std::string layoutBefore;
    std::string layoutAfter;
    std::string writeStage;
    std::string writeAccess;
    std::string objectSha256;
};

struct FramebufferEvidence {
    uint64_t renderPass = 0;
    uint64_t framebuffer = 0;
    uint64_t colorImageView = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t attachmentCount = 1;
    std::string format;
    std::string clearColor;
    std::string contentSha256;
    std::string contentCrc32c;
};

struct RenderEvidence {
    RenderEvidence() = default;
    uint32_t objectId = 0;
    std::string backend;
    std::string deviceName;
    std::string driverName;
    DrawCallEvidence drawCall;
    ResourceStateEvidence uniformBuffer;
    ResourceStateEvidence colorTarget;
    FramebufferEvidence framebuffer;
    RgbaImage image;
};

class VulkanReviewRenderer {
public:
    VulkanReviewRenderer(uint32_t width, uint32_t height,
                         std::vector<std::string> instanceExtensions);
    ~VulkanReviewRenderer();

    VulkanReviewRenderer(const VulkanReviewRenderer&) = delete;
    VulkanReviewRenderer& operator=(const VulkanReviewRenderer&) = delete;

    void initialize();
    void shutdown();
    RenderEvidence renderShadow(const ShadowRecord& record,
                                const std::string& side);

    bool initialized() const { return m_initialized; }
    uint32_t liveTransientObjects() const { return m_liveTransientObjects; }
    const std::string& deviceName() const { return m_deviceName; }
    const std::string& driverName() const { return m_driverName; }
    std::string vulkanApiVersion() const;

private:
    struct Transient;

    void createInstance(std::vector<std::string> extensions);
    void pickDevice();
    void createDevice();
    void createRenderResources();
    void createDescriptorResources();
    void createPipeline();
    void createCommandResources();
    RenderEvidence renderOnce(const ShadowRecord& record,
                              const std::string& side, Transient& transient);
    void releaseTransient(Transient& transient);
    void check(VkResult result, const char* operation) const;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_initialized = false;
    uint32_t m_liveTransientObjects = 0;
    std::string m_deviceName;
    std::string m_driverName;
    uint32_t m_apiVersion = VK_API_VERSION_1_0;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    VkPhysicalDeviceMemoryProperties m_memoryProperties{};

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkShaderModule m_vertexShader = VK_NULL_HANDLE;
    VkShaderModule m_fragmentShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
};

}  // namespace lshc
