#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

#include "util.h"

struct GLFWwindow;

namespace tar {

#define VK_CHECK(expr)                                                    \
  do {                                                                    \
    VkResult res_ = (expr);                                               \
    if (res_ != VK_SUCCESS) {                                             \
      throw ::tar::PipelineError(std::string("Vulkan error ") +           \
                                 std::to_string(static_cast<int>(res_)) + \
                                 " at " #expr);                           \
    }                                                                     \
  } while (0)

// Headless Vulkan context plus a GLFW window handle. Rendering is fully
// offscreen (no swapchain); GLFW provides the windowing side of the stack
// and uses the Null platform when no display server is available.
class VkContext {
 public:
  explicit VkContext(Log& log);
  ~VkContext();

  VkContext(const VkContext&) = delete;
  VkContext& operator=(const VkContext&) = delete;

  VkDevice Device() const { return device_; }
  VkPhysicalDevice PhysicalDevice() const { return physical_; }
  VkQueue Queue() const { return queue_; }
  uint32_t QueueFamily() const { return queue_family_; }
  VkCommandPool CommandPool() const { return cmd_pool_; }
  const std::string& DeviceName() const { return device_name_; }

  void PollEvents();  // pumps GLFW events for the hidden window

  uint32_t FindMemoryType(uint32_t type_bits, VkMemoryPropertyFlags props) const;

  // One-shot command buffer helpers.
  VkCommandBuffer BeginCommands();
  void EndCommands(VkCommandBuffer cmd);

 private:
  Log& log_;
  GLFWwindow* window_ = nullptr;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t queue_family_ = 0;
  VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
  std::string device_name_;
};

}  // namespace tar
