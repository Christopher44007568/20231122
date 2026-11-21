#pragma once

#define GLFW_INCLUDE_NONE
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace gcdump::gui {

// Minimal headless-friendly Vulkan renderer. It clears the framebuffer with a
// status color each frame; all inspection text is rasterized separately into a
// CPU-side RGBA bitmap by inspection_page and uploaded as a host-visible image
// in a follow-up step. The renderer keeps no domain logic.
class VulkanRenderer {
public:
    using DrawCallback = std::function<void(VkCommandBuffer, VkExtent2D)>;
    using OutsidePassCallback =
        std::function<void(VkCommandBuffer, VkImage, VkExtent2D)>;

    explicit VulkanRenderer(GLFWwindow* window);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void setClearColor(std::array<float, 4> color);
    void drawFrame(DrawCallback callback);
    void setOutsidePassCallback(OutsidePassCallback cb) {
        outsidePass_ = std::move(cb);
    }
    void waitIdle();

    VkRenderPass renderPass() const { return renderPass_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    uint32_t graphicsFamily() const { return families_.graphics; }

    VkDevice device() const { return device_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }

private:
    struct QueueFamilies {
        uint32_t graphics = UINT32_MAX;
        uint32_t present = UINT32_MAX;
        bool complete() const {
            return graphics != UINT32_MAX && present != UINT32_MAX;
        }
    };

    struct FrameData {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
    };

    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void createImageViews();
    void createRenderPass();
    void createFramebuffers();
    void createCommandPoolAndBuffers();
    void createSyncObjects();
    void destroySwapchainObjects();
    QueueFamilies findQueueFamilies(VkPhysicalDevice dev) const;

    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence inFlight_ = VK_NULL_HANDLE;
    QueueFamilies families_;
    std::vector<FrameData> frames_;
    std::array<float, 4> clearColor_{0.05f, 0.06f, 0.08f, 1.0f};
    OutsidePassCallback outsidePass_;
};

} // namespace gcdump::gui
