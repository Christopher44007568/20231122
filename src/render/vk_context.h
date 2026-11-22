#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ssor {

struct GpuBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
    bool hostVisible = false;
};

struct GpuImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::uint32_t bytesPerPixel = 0;
};

struct GpuContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkPhysicalDeviceProperties properties{};
    bool timestampValid = false;
    float timestampPeriod = 0.0f;
    std::string glfwVersion;
    int glfwPlatform = -1;
    bool validationEnabled = false;
    std::string icdPath;
};

bool initGpuContext(GpuContext& context, std::string* error);
void shutdownGpuContext(GpuContext& context);

std::uint32_t findMemoryType(const GpuContext& context, std::uint32_t typeBits,
                             VkMemoryPropertyFlags properties);
bool createBuffer(const GpuContext& context, VkDeviceSize size,
                  VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                  GpuBuffer& buffer, std::string* error);
void destroyBuffer(const GpuContext& context, GpuBuffer& buffer);
bool createImage(const GpuContext& context, std::uint32_t width,
                 std::uint32_t height, VkFormat format,
                 VkImageUsageFlags usage, GpuImage& image, std::string* error);
void destroyImage(const GpuContext& context, GpuImage& image);

VkShaderModule createShaderModule(const GpuContext& context,
                                  const std::uint32_t* words,
                                  std::size_t byteSize,
                                  std::string* error);

VkCommandBuffer beginOneShot(const GpuContext& context, std::string* error);
bool endOneShot(const GpuContext& context, VkCommandBuffer commandBuffer,
                std::string* error);
bool waitIdle(const GpuContext& context, std::string* error);

std::string vulkanFormatName(VkFormat format);
std::string vulkanLayoutName(VkImageLayout layout);
std::string vulkanStageName(VkPipelineStageFlags stage);
std::string vulkanAccessName(VkAccessFlags access);
std::string vulkanResultName(VkResult result);
std::uint32_t bytesPerPixel(VkFormat format);

}  // namespace ssor
