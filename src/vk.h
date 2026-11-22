#pragma once
#include "util.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace pcd {

struct VkBuffer2 {
  VkBuffer buf = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
};

// Minimal Vulkan context: one queue family with compute+graphics.
struct VkContext {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queue_family = 0;
  VkCommandPool cmd_pool = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties props{};
  std::string device_name;
};

// If with_surface_exts is true, instance extensions required by GLFW
// are enabled (viewer mode). Headless runs pass false.
VkContext vk_create(bool with_surface_exts);
void vk_destroy(VkContext& ctx);
VkBuffer2 vk_buffer(VkContext& ctx, VkDeviceSize size,
                    VkBufferUsageFlags usage, bool host_visible);
void vk_buffer_destroy(VkContext& ctx, VkBuffer2& b);
void vk_upload(VkContext& ctx, VkBuffer2& dst, const void* data,
               VkDeviceSize size);
void vk_download(VkContext& ctx, VkBuffer2& src, void* data,
                 VkDeviceSize size);
VkShaderModule vk_shader(VkContext& ctx, const std::string& spv_path);

// One-shot command buffer submit + wait.
void vk_one_shot(VkContext& ctx,
                 const std::function<void(VkCommandBuffer)>& record);

#define VK_CHECK(x)                                                    \
  do {                                                                 \
    VkResult _r = (x);                                                 \
    if (_r != VK_SUCCESS)                                              \
      ::pcd::die(std::string("Vulkan error ") + std::to_string(_r) +   \
                 " at " #x);                                           \
  } while (0)

} // namespace pcd
