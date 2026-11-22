#pragma once

#include "common.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace inst {

inline const char* vkResultName(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        default: return "VK_ERROR_OTHER";
    }
}

#define INST_VK_CHECK(expr)                                                  \
    do {                                                                     \
        const VkResult checked_ = (expr);                                    \
        if (checked_ != VK_SUCCESS) {                                        \
            throw RenderError(std::string(vkResultName(checked_)) +          \
                              " at " + __FILE__ + ":" +                      \
                              std::to_string(__LINE__));                     \
        }                                                                    \
    } while (0)

class VulkanContext {
public:
    VulkanContext(uint32_t width, uint32_t height);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkDevice device() const { return device_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkQueue queue() const { return queue_; }
    VkCommandPool commandPool() const { return commandPool_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    const std::string& deviceName() const { return deviceName_; }

    VkCommandBuffer beginOneShot() const;
    void endAndWait(VkCommandBuffer commandBuffer) const;
    VkShaderModule loadShader(const uint32_t* words, size_t wordCount) const;
    uint32_t findMemoryType(uint32_t typeBits,
                            VkMemoryPropertyFlags properties) const;

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::string deviceName_;
    GLFWwindow* window_ = nullptr;
};

struct VkBufferResource {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

VkBufferResource createBufferResource(const VulkanContext& context,
                                      VkDeviceSize size,
                                      VkBufferUsageFlags usage,
                                      VkMemoryPropertyFlags properties);
void destroyBufferResource(const VulkanContext& context,
                           VkBufferResource& resource);
void uploadBuffer(const VulkanContext& context,
                  const VkBufferResource& destination,
                  const void* source, VkDeviceSize size);

} // namespace inst
