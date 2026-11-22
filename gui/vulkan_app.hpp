#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "camtrak/types.hpp"

namespace camtrak::gui {

struct RenderLine {
  std::vector<Vec3> points;
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
};

// Minimal Vulkan line renderer for trajectory review. It owns the instance,
// device, swapchain, pipeline and line buffers, and degrades with a clear
// error message when no GPU/surface is available (e.g. headless CI).
class VulkanApp {
 public:
  VulkanApp();
  ~VulkanApp();

  VulkanApp(const VulkanApp&) = delete;
  VulkanApp& operator=(const VulkanApp&) = delete;

  bool initialize(GLFWwindow* window);
  void drawFrame(const std::vector<RenderLine>& lines);
  void shutdown();

 private:
  struct QueueFamilies {
    std::uint32_t graphics = UINT32_MAX;
    std::uint32_t present = UINT32_MAX;
    bool complete() const {
      return graphics != UINT32_MAX && present != UINT32_MAX;
    }
  };

  struct PushConstants {
    float view_proj[16];
    float color[4];
  };

  GLFWwindow* window_ = nullptr;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue graphics_queue_ = VK_NULL_HANDLE;
  VkQueue present_queue_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
  VkExtent2D extent_{};
  std::vector<VkImage> swapchain_images_;
  std::vector<VkImageView> image_views_;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers_;
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> command_buffers_;
  std::vector<VkSemaphore> image_available_;
  std::vector<VkSemaphore> render_finished_;
  std::vector<VkFence> in_flight_;
  std::uint32_t frame_index_ = 0;
  std::uint32_t image_index_ = 0;
  VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
  std::size_t vertex_buffer_capacity_ = 0;

  void ensureVertexCapacity(std::size_t byte_count);

  QueueFamilies findQueueFamilies(VkPhysicalDevice device) const;
  void createSwapchain();
  void createImageViews();
  void createRenderPass();
  void createPipeline();
  void createFramebuffers();
  void createCommandPool();
  void createCommandBuffers();
  void createSyncObjects();
  void recordCommands(VkCommandBuffer command_buffer,
                      const std::vector<RenderLine>& lines);
};

}  // namespace camtrak::gui
