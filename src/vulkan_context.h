#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <cstdint>
#include <string>

namespace vox {

// Owns the process-wide Vulkan/GLFW state. Constructed once per run and torn
// down at the very end so temporary state never leaks across objects.
class VulkanContext {
public:
  VulkanContext(uint32_t width, uint32_t height);
  ~VulkanContext();

  VulkanContext(const VulkanContext&) = delete;
  VulkanContext& operator=(const VulkanContext&) = delete;

  VkDevice device() const { return device_; }
  VkPhysicalDevice physical() const { return physical_; }
  VkQueue queue() const { return queue_; }
  uint32_t queue_family() const { return queue_family_; }
  VkCommandPool command_pool() const { return command_pool_; }
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  const std::string& device_name() const { return device_name_; }
  uint32_t api_version() const { return api_version_; }

  // One-shot command recording helper for transfer/render operations.
  VkCommandBuffer begin_oneshot();
  void end_oneshot(VkCommandBuffer cmd);

private:
  uint32_t width_;
  uint32_t height_;
  GLFWwindow* window_ = nullptr;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  uint32_t queue_family_ = 0;
  std::string device_name_;
  uint32_t api_version_ = 0;
};

void vk_check(VkResult result, const char* what);

} // namespace vox
