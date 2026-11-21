#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace skr {

#define VK_CHECK(expr)                                                        \
    do {                                                                      \
        VkResult result_ = (expr);                                            \
        if (result_ != VK_SUCCESS)                                            \
            throw std::runtime_error(std::string("Vulkan error ") +          \
                                     std::to_string(result_) + " at " +      \
                                     __FILE__ + ":" +                        \
                                     std::to_string(__LINE__));              \
    } while (0)

// RAII Vulkan context. Headless by design: GLFW is initialized on its null
// platform and no VkSurfaceKHR / swapchain is ever created. Rendering targets
// an offscreen framebuffer that is read back through a staging buffer.
class VulkanContext {
public:
    VulkanContext(uint32_t width, uint32_t height);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkDevice device() const { return device_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkQueue queue() const { return queue_; }
    uint32_t queueFamily() const { return queueFamily_; }
    VkCommandPool commandPool() const { return commandPool_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    const std::string& deviceName() const { return deviceName_; }

    VkCommandBuffer beginOneShot() const;
    void endAndWait(VkCommandBuffer cmd) const;

    VkShaderModule loadShader(const uint32_t* code, size_t codeSizeWords) const;
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::string deviceName_;
    void* glfwWindow_ = nullptr; // GLFWwindow* kept opaque here
};

} // namespace skr
