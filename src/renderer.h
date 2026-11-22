#pragma once

#include "clock_model.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;

namespace ph {

class VulkanRenderer {
public:
    VulkanRenderer() = default;
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    bool initialize(int width, int height, GLFWwindow* window, std::string& error);
    void destroy();

    bool renderFrame(int64_t scene_tick_us, const ClockConfig& config, uint32_t device_index,
                     std::vector<uint8_t>& rgba, std::string& error);
    bool present(std::string& error);

    bool hasSurface() const { return surface_ != VK_NULL_HANDLE; }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::string& deviceName() const { return device_name_; }
    const std::string& apiVersion() const { return api_version_; }

private:
    bool createInstance(std::string& error);
    bool pickPhysicalDevice(std::string& error);
    bool createDevice(std::string& error);
    bool createSwapchain(std::string& error);
    void destroySwapchain();
    bool createOffscreenTarget(std::string& error);
    bool createPipeline(std::string& error);
    bool createCommands(std::string& error);
    uint32_t findMemoryType(uint32_t type_bits, VkMemoryPropertyFlags properties,
                            bool& found) const;
    VkShaderModule createShaderModule(const uint32_t* words, size_t word_count,
                                      std::string& error) const;
    void transitionImage(VkCommandBuffer command, VkImage image, VkImageLayout old_layout,
                         VkImageLayout new_layout, VkAccessFlags src_access,
                         VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
                         VkPipelineStageFlags dst_stage);
    bool recordAndSubmitRender(int64_t scene_tick_us, const ClockConfig& config,
                               uint32_t device_index, std::string& error);

    int width_ = 0;
    int height_ = 0;
    GLFWwindow* window_ = nullptr;
    std::string device_name_;
    std::string api_version_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;

    VkImage color_image_ = VK_NULL_HANDLE;
    VkDeviceMemory color_memory_ = VK_NULL_HANDLE;
    VkImageView color_view_ = VK_NULL_HANDLE;
    VkImageLayout color_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkBuffer readback_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory_ = VK_NULL_HANDLE;
    void* readback_mapped_ = nullptr;
    bool readback_coherent_ = false;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent_{0, 0};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_views_;
    std::vector<VkImageLayout> swapchain_layouts_;
    VkSemaphore image_available_ = VK_NULL_HANDLE;
    VkSemaphore render_finished_ = VK_NULL_HANDLE;

    PFN_vkCreateSwapchainKHR create_swapchain_ = nullptr;
    PFN_vkDestroySwapchainKHR destroy_swapchain_ = nullptr;
    PFN_vkGetSwapchainImagesKHR get_swapchain_images_ = nullptr;
    PFN_vkAcquireNextImageKHR acquire_next_image_ = nullptr;
    PFN_vkQueuePresentKHR queue_present_ = nullptr;
};

}  // namespace ph
