#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal headless Vulkan context: instance + one device + one queue.
// Rendering is fully offscreen, so no surface/swapchain is required; GLFW
// (used by the app for optional on-screen preview) is not needed here.
class VkContext {
public:
  VkContext();
  ~VkContext();
  VkContext(const VkContext&) = delete;
  VkContext& operator=(const VkContext&) = delete;

  VkDevice device() const { return device_; }
  VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
  VkQueue queue() const { return queue_; }
  VkCommandPool commandPool() const { return commandPool_; }
  const std::string& deviceName() const { return deviceName_; }

  uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

  // Runs fn(cmd) inside a one-shot command buffer and waits for completion.
  template <typename Fn>
  void oneShot(Fn&& fn) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = commandPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &ai, &cmd) != VK_SUCCESS)
      throw std::runtime_error("vkAllocateCommandBuffers failed");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    fn(cmd);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    if (vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS ||
        vkQueueWaitIdle(queue_) != VK_SUCCESS) {
      vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
      throw std::runtime_error("command submission failed");
    }
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
  }

private:
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  uint32_t queueFamily_ = 0;
  std::string deviceName_;
};
