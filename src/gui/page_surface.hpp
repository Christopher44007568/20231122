#pragma once
#include "vulkan_renderer.hpp"
#include <cstdint>

namespace gcdump::gui {

// Host-visible RGBA image holding the CPU-rasterized inspection page. It is
// blitted into the swapchain with vkCmdBlitImage, so no shader toolchain or
// vertex/pipeline objects are required at build or run time.
class PageSurface {
public:
    PageSurface(VkPhysicalDevice physical, VkDevice device);
    ~PageSurface();

    PageSurface(const PageSurface&) = delete;
    PageSurface& operator=(const PageSurface&) = delete;

    void upload(const uint8_t* rgba, uint32_t width, uint32_t height);
    void blitTo(VkCommandBuffer cmd, VkImage target,
                VkExtent2D targetExtent);

private:
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props);
    void destroyImage();
    void ensureImage(uint32_t width, uint32_t height);

    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

} // namespace gcdump::gui
