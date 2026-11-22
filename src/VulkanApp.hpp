#pragma once

#include "DebugUI.hpp"
#include "PagingModel.hpp"
#include "Scenarios.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;

namespace tpd {

class VulkanApp {
public:
    VulkanApp();
    ~VulkanApp();

    int run();

private:
    static constexpr uint32_t kPageSize = 256;
    static constexpr int kMaxFramesInFlight = 1;

    struct QueueFamilies {
        uint32_t graphics = UINT32_MAX;
        uint32_t present = UINT32_MAX;
        bool complete() const {
            return graphics != UINT32_MAX && present != UINT32_MAX;
        }
    };

    struct SwapchainSupport {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct ScenePushConstants {
        int layerIndex;
        int mode;
    };

    GLFWwindow* window_ = nullptr;
    bool framebufferResized_ = false;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    QueueFamilies queueFamilies_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainViews_;
    std::vector<VkFramebuffer> framebuffers_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet arrayDescriptorSet_ = VK_NULL_HANDLE;
    VkDescriptorSet uiDescriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout scenePipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout uiPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline scenePipeline_ = VK_NULL_HANDLE;
    VkPipeline uiPipeline_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence frameFence_ = VK_NULL_HANDLE;

    VkImage arrayImage_ = VK_NULL_HANDLE;
    VkDeviceMemory arrayMemory_ = VK_NULL_HANDLE;
    VkImageView arrayView_ = VK_NULL_HANDLE;
    VkSampler arraySampler_ = VK_NULL_HANDLE;

    VkImage uiImage_ = VK_NULL_HANDLE;
    VkDeviceMemory uiMemory_ = VK_NULL_HANDLE;
    VkImageView uiView_ = VK_NULL_HANDLE;
    VkSampler uiSampler_ = VK_NULL_HANDLE;
    VkBuffer uiStagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uiStagingMemory_ = VK_NULL_HANDLE;
    void* uiStagingMapped_ = nullptr;

    PagingModel model_;
    std::vector<Scenario> scenarios_;
    size_t scenarioIndex_ = 0;
    size_t replayStep_ = 0;
    uint64_t nextReplayTick_ = 0;
    double speed_ = 1.0;
    bool playing_ = true;
    bool resetRequested_ = false;
    size_t requestedScenario_ = 0;
    int stepDirection_ = 0;
    double tickRemainder_ = 0.0;
    float smoothedFps_ = 60.0f;
    size_t nextTraceToPrint_ = 0;
    std::vector<uint8_t> uiPixels_;

    DebugUI ui_;

    bool initWindow();
    bool initVulkan();
    void resetScenario(size_t index);
    void advanceReplay();
    void requestReplayPage(size_t step);
    void processStartedUploads();
    void pumpSwitchOutput();
    void handleInput();
    void mainLoop();
    void drawFrame();
    void cleanup();

    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void createImageViews();
    void createRenderPass();
    void createDescriptorSetLayout();
    void createPipelines();
    void createCommandResources();
    void createTextureArray();
    void createUiTexture();
    void createDescriptors();
    void createSwapchainResources();
    void cleanupSwapchain();
    void recreateSwapchain();

    QueueFamilies findQueueFamilies(VkPhysicalDevice device) const;
    SwapchainSupport querySwapchainSupport(VkPhysicalDevice device) const;
    bool isDeviceSuitable(VkPhysicalDevice device) const;
    VkSurfaceFormatKHR chooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR choosePresentMode(
        const std::vector<VkPresentModeKHR>& modes) const;
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps) const;

    VkShaderModule loadShader(const std::string& path) const;
    VkShaderModule createShaderModule(const std::vector<char>& code) const;
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties, VkBuffer& buffer,
                      VkDeviceMemory& memory);
    void createImage(uint32_t width, uint32_t height, uint32_t layers,
                     VkFormat format, VkImageUsageFlags usage, VkImage& image,
                     VkDeviceMemory& memory);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    void transitionImageLayout(VkImage image, VkFormat format,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               uint32_t baseLayer, uint32_t layerCount);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                           uint32_t height, uint32_t baseLayer);
    void uploadLayerContent(int layer, int contentId);
    void uploadUiPixels(const std::vector<uint8_t>& pixels);
    std::vector<uint8_t> generatePagePixels(int contentId) const;

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void drawScene(VkCommandBuffer commandBuffer);

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action,
                            int mods);
};

}  // namespace tpd
