#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>

// Result of the "unknown extension" control experiment.
struct ExtControlReport {
  std::string unknownInstanceExt = "VK_EXT_csm_lab_unknown_control";
  std::string unknownDeviceExt = "VK_EXT_csm_lab_unknown_device_control";
  bool unknownInstanceExtSupported = true;   // expect false
  bool unknownDeviceExtSupported = true;     // expect false
  VkResult deviceCreateWithUnknownExt = VK_SUCCESS; // expect VK_ERROR_EXTENSION_NOT_PRESENT
  VkResult deviceCreateClean = VK_SUCCESS;    // expect VK_SUCCESS
  std::vector<std::string> instanceExts;
  std::vector<std::string> deviceExts;
};

struct VkContext {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VkQueue queue = VK_NULL_HANDLE;
  VkCommandPool cmdPool = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties memProps{};
  std::string deviceName;
  uint32_t apiVersion = 0;
  ExtControlReport extControl;
};

// Creates instance + picks device. If enableUnknownExtControl, first probes
// the unknown extensions as a control group and records outcomes.
bool vkInit(VkContext& ctx, const std::vector<std::string>& instanceExts,
            bool enableUnknownExtControl, std::string& err,
            const std::vector<std::string>& deviceExts = {});
void vkDestroy(VkContext& ctx);

uint32_t findMemoryType(const VkContext& ctx, uint32_t typeBits,
                        VkMemoryPropertyFlags props);

struct GpuBuffer {
  VkBuffer buf = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
};

GpuBuffer createBuffer(const VkContext& ctx, VkDeviceSize size,
                       VkBufferUsageFlags usage, VkMemoryPropertyFlags props);
void destroyBuffer(const VkContext& ctx, GpuBuffer& b);

struct GpuImage {
  VkImage img = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t width = 0, height = 0;
};

GpuImage createImage2D(const VkContext& ctx, uint32_t w, uint32_t h,
                       VkFormat format, VkImageUsageFlags usage,
                       VkImageAspectFlags aspect);
void destroyImage(const VkContext& ctx, GpuImage& im);

VkCommandBuffer beginOneShot(const VkContext& ctx);
void endOneShot(const VkContext& ctx, VkCommandBuffer cmd);

VkShaderModule loadShader(const VkContext& ctx, const std::string& spvPath);

void transitionImage(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect,
                     VkImageLayout oldL, VkImageLayout newL);

// Reads back a color image (assumed SHADER_READ_ONLY/TRANSFER_SRC optimal)
// into RGBA8 host pixels.
bool readbackImageRgba(const VkContext& ctx, GpuImage& im,
                       std::vector<uint8_t>& outRgba);
