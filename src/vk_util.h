#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define VK_CHECK(expr)                                                        \
    do {                                                                      \
        VkResult res_ = (expr);                                               \
        if (res_ != VK_SUCCESS) {                                             \
            std::fprintf(stderr, "VK_CHECK failed (%d) at %s:%d: %s\n",       \
                         (int)res_, __FILE__, __LINE__, #expr);               \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

struct Gpu {
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memProps{};
};

uint32_t findMemoryType(const Gpu& gpu, uint32_t typeBits, VkMemoryPropertyFlags props);

struct Buffer {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

Buffer createBuffer(const Gpu& gpu, VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags props);
void destroyBuffer(const Gpu& gpu, Buffer& b);

struct Image {
    VkImage img = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0, height = 0;
};

Image createImage2D(const Gpu& gpu, uint32_t w, uint32_t h, VkFormat format,
                    VkImageUsageFlags usage, VkImageAspectFlags aspect);
void destroyImage(const Gpu& gpu, Image& im);

VkShaderModule loadShaderModule(VkDevice device, const std::string& path);

void transitionImage(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect,
                     VkImageLayout oldLayout, VkImageLayout newLayout);

VkCommandBuffer beginOneShot(VkDevice device, VkCommandPool pool);
void endOneShot(VkDevice device, VkCommandPool pool, VkQueue queue, VkCommandBuffer cmd);
