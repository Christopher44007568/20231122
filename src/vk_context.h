#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;

// Owns the Vulkan instance/device and per-frame submission helpers.
// All rendering in this tool is offscreen (depth attachments); GLFW is
// used for the optional interactive viewer window.
class VkContext {
public:
  bool init(bool enableValidation,
            const std::vector<const char*>& extraInstanceExts = {});
  void shutdown();

  VkDevice device() const { return device_; }
  VkInstance instance() const { return instance_; }
  VkPhysicalDevice physicalDevice() const { return phys_; }
  VkQueue queue() const { return queue_; }
  uint32_t queueFamily() const { return queueFamily_; }
  VkCommandPool commandPool() const { return cmdPool_; }
  float timestampPeriodNs() const { return timestampPeriodNs_; }
  bool timestampsSupported() const { return timestampValidBits_ != 0; }

  // One-shot command buffer helpers (the tool is not latency sensitive;
  // cache refreshes are submitted and waited individually so timings are
  // attributable to a single cache level).
  VkCommandBuffer beginCommands();
  void endCommands(VkCommandBuffer cmd); // submits and waits idle

  uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

  const std::string& deviceName() const { return deviceName_; }

private:
  VkInstance instance_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
  VkPhysicalDevice phys_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t queueFamily_ = 0;
  VkCommandPool cmdPool_ = VK_NULL_HANDLE;
  float timestampPeriodNs_ = 1.0f;
  uint32_t timestampValidBits_ = 0;
  std::string deviceName_;
  bool validation_ = false;
};

// Small RAII helpers shared by the cache/analyzer code.
struct GpuImage {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  uint32_t width = 0;
  uint32_t height = 0;
  VkFormat format = VK_FORMAT_UNDEFINED;

  void destroy(VkDevice dev);
};

struct GpuBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  void* mapped = nullptr;

  void destroy(VkDevice dev);
};

bool createImage(VkContext& ctx, uint32_t w, uint32_t h, VkFormat format,
                 VkImageUsageFlags usage, VkImageAspectFlags aspect,
                 GpuImage& out);
bool createBuffer(VkContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags props, GpuBuffer& out);
void transitionImage(VkCommandBuffer cmd, VkImage image,
                     VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkImageAspectFlags aspect);
