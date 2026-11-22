#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include "png.h"

// Minimal headless-friendly Vulkan context (llvmpipe-safe, no swapchain).
struct Gpu {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkDevice dev = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t qfam = 0;
  VkCommandPool pool = VK_NULL_HANDLE;

  void init();
  void destroy();

  VkCommandBuffer beginCmd();
  void submitWait(VkCommandBuffer cmd);  // submit, wait idle, free
  uint32_t memType(uint32_t bits, VkMemoryPropertyFlags props);
};

struct GpuImage {
  VkImage img = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  uint32_t w = 0, h = 0;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

GpuImage createImage(Gpu& g, uint32_t w, uint32_t h, VkImageUsageFlags usage);
void destroyImage(Gpu& g, GpuImage& im);
void transition(Gpu& g, GpuImage& im, VkImageLayout to);
// Copies the image to host memory. `restore` is the layout to return to.
Image readback(Gpu& g, GpuImage& im, VkImageLayout restore);

struct GpuBuffer {
  VkBuffer buf = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  void* mapped = nullptr;
  VkDeviceSize size = 0;
};

GpuBuffer createBuffer(Gpu& g, VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags props, bool map);
void destroyBuffer(Gpu& g, GpuBuffer& b);
