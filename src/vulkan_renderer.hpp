#pragma once

#include "common.hpp"
#include "state.hpp"

#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace offscreen {

struct RenderConfig {
    std::uint32_t width = 320;
    std::uint32_t height = 200;
};

struct Capture {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Bytes rgba;
    std::string sha256;
};

class VulkanRenderer {
public:
    explicit VulkanRenderer(const RenderConfig& config);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    Capture capture(const VisualState& state);
    const std::string& deviceName() const { return deviceName_; }
    const std::string& driverName() const { return driverName_; }
    const std::string& apiVersion() const { return apiVersion_; }
    const std::string& glfwPlatform() const { return glfwPlatform_; }
    const std::string& vulkanFormat() const { return vulkanFormat_; }

private:
    void createInstance();
    void pickDevice();
    void createDevice();
    void createTarget();
    void createPipeline();
    void createCommands();
    void destroy();
    void submitAndWait();

    RenderConfig config_;
    GLFWwindow* window_ = nullptr;
    bool glfwReady_ = false;
    std::string deviceName_;
    std::string driverName_;
    std::string apiVersion_;
    std::string glfwPlatform_;
    std::string vulkanFormat_ = "VK_FORMAT_R8G8B8A8_UNORM";

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkBuffer stagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
};

}  // namespace offscreen
