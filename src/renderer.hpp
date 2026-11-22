#pragma once

#include "checkpoint.hpp"
#include "config.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

struct GLFWwindow;

struct FrameSnapshot {
    ImageData image;
    ImageMetrics metrics;
    std::array<uint32_t, 4> gpuStats{};
    uint64_t frameIndex = 0;
};

class VulkanFogRenderer {
public:
    VulkanFogRenderer(FogConfig config, RunOptions options);
    ~VulkanFogRenderer();

    VulkanFogRenderer(const VulkanFogRenderer&) = delete;
    VulkanFogRenderer& operator=(const VulkanFogRenderer&) = delete;

    void initialize();
    void pollEvents();
    bool shouldClose() const;
    void requestClose();

    void renderFrame(uint64_t frameIndex, bool present = true);
    FrameSnapshot readbackSnapshot();
    CheckpointData saveCheckpoint(const std::string& path,
                                  uint64_t nextFrame);
    void restoreCheckpoint(const CheckpointData& checkpoint);

    const MemoryPlan& plan() const { return plan_; }
    const std::string& deviceName() const { return deviceName_; }
    const std::string& deviceType() const { return deviceType_; }
    uint64_t deviceMemoryBytes() const { return deviceMemoryBytes_; }
    uint64_t memoryBudgetBytes() const { return memoryBudgetBytes_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    GLFWwindow* window() const { return window_; }
    uint64_t lastFrameIndex() const { return lastFrameIndex_; }

private:
    struct VolumeResources {
        VkImage images[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
        VkDeviceMemory imageMemory[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
        VkImageView imageViews[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
        VkBuffer readbackBuffer = VK_NULL_HANDLE;
        VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
        void* readbackMapped = nullptr;
        VkDeviceSize readbackBytes = 0;
    };

    void createWindow();
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createDevice();
    void createSwapchain();
    void createSwapchainImageViews();
    void createRenderPass();
    void createFramebuffers();
    void createShaderModules();
    void createDescriptorLayouts();
    void createPipelines();
    void createDisplayPipeline();
    void createUniformAndStatsBuffers();
    void createDescriptorPool();
    void createVolumeResources();
    void createVolumeImages(const MemoryPlan& plan, VolumeResources& resources);
    void createVolumeBuffers(const MemoryPlan& plan, VolumeResources& resources);
    void createComputeDescriptors();
    void createDisplayDescriptors();
    void clearVolumeImages(VolumeResources& resources);
    void destroyVolumeResources();
    void destroySwapchainDependent();
    void recreateSwapchain();

    FogUniforms makeUniforms(uint64_t frameIndex) const;
    VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;
    void transitionImage(VkCommandBuffer commandBuffer, VkImage image,
                         VkImageLayout oldLayout, VkImageLayout newLayout,
                         VkAccessFlags sourceAccess, VkAccessFlags destinationAccess,
                         VkPipelineStageFlags sourceStage,
                         VkPipelineStageFlags destinationStage) const;
    VkShaderModule createShaderModule(const std::vector<char>& code) const;
    std::vector<char> readFile(const std::string& path) const;
    void uploadCheckpointImage(const CheckpointData& checkpoint);
    void checkResult(VkResult result, const char* operation) const;
    uint32_t findMemoryType(uint32_t typeBits,
                            VkMemoryPropertyFlags properties) const;
    void copyImageToReadback();

    FogConfig config_;
    RunOptions options_;
    MemoryPlan plan_;
    std::vector<std::string> warnings_;
    std::string deviceName_;
    std::string deviceType_;
    uint64_t deviceMemoryBytes_ = 0;
    uint64_t memoryBudgetBytes_ = 0;

    GLFWwindow* window_ = nullptr;
    bool glfwReady_ = false;
    bool initialized_ = false;
    bool framebufferResized_ = false;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memoryProperties_{};

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    std::vector<VkFramebuffer> framebuffers_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence frameFence_ = VK_NULL_HANDLE;

    VkShaderModule fogShader_ = VK_NULL_HANDLE;
    VkShaderModule displayVertShader_ = VK_NULL_HANDLE;
    VkShaderModule displayFragShader_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout computeSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout displaySetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout displayPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline computePipeline_ = VK_NULL_HANDLE;
    VkPipeline displayPipeline_ = VK_NULL_HANDLE;

    VkBuffer uniformBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory_ = VK_NULL_HANDLE;
    void* uniformMapped_ = nullptr;
    VkBuffer statsBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory statsMemory_ = VK_NULL_HANDLE;
    void* statsMapped_ = nullptr;

    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet computeSets_[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorSet displaySets_[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkSampler fogSampler_ = VK_NULL_HANDLE;

    VolumeResources volume_;
    int currentImageIndex_ = 0;
    uint64_t lastFrameIndex_ = 0;
    bool restoredUniformsValid_ = false;
    FogUniforms restoredUniforms_{};
    std::array<uint32_t, 4> lastGpuStats_{};
};
