#pragma once

#include "app/render_types.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;

namespace vfcr {

class VulkanRenderer {
public:
    explicit VulkanRenderer(GLFWwindow* window);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    CaptureResult draw(const std::vector<UiVertex>& vertices, const std::string& capturePath);
    VkExtent2D extent() const { return extent_; }

private:
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createDevice();
    void createSwapchain();
    void createImageViews();
    void createRenderPass();
    void createPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffer();
    void createSyncObjects();
    void createVertexBuffer(std::size_t capacity);
    void ensureVertexCapacity(std::size_t count);
    void cleanupSwapchain();
    void recreateSwapchain();
    void loadShaderModules();
    VkShaderModule createShaderModule(const std::string& path) const;
    void transitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout,
                         VkImageLayout newLayout, VkAccessFlags sourceAccess,
                         VkAccessFlags destinationAccess, VkPipelineStageFlags sourceStage,
                         VkPipelineStageFlags destinationStage);
    std::uint32_t findMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const;
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    bool framebufferResized(int* width, int* height) const;

    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    std::uint32_t graphicsFamily_ = 0;
    std::uint32_t presentFamily_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainViews_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkSemaphore> renderFinished_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkShaderModule vertexShader_ = VK_NULL_HANDLE;
    VkShaderModule fragmentShader_ = VK_NULL_HANDLE;

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    void* vertexMapped_ = nullptr;
    std::size_t vertexCapacity_ = 0;
    uint32_t currentImageIndex_ = 0;
};

}  // namespace vfcr
