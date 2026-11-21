#include "vulkan_renderer.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

namespace gcdump::gui {

namespace {

void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string("Vulkan failure: ") + what +
                                 " (VkResult=" + std::to_string(result) + ")");
}

} // namespace

VulkanRenderer::VulkanRenderer(GLFWwindow* window) : window_(window) {
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createCommandPoolAndBuffers();
    createFramebuffers();
    createSyncObjects();
}

VulkanRenderer::~VulkanRenderer() {
    if (device_ == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_);
    vkDestroySemaphore(device_, imageAvailable_, nullptr);
    vkDestroySemaphore(device_, renderFinished_, nullptr);
    vkDestroyFence(device_, inFlight_, nullptr);
    vkDestroyCommandPool(device_, commandPool_, nullptr);
    destroySwapchainObjects();
    vkDestroyRenderPass(device_, renderPass_, nullptr);
    vkDestroyDevice(device_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    vkDestroyInstance(instance_, nullptr);
}

void VulkanRenderer::setClearColor(std::array<float, 4> color) {
    clearColor_ = color;
}

void VulkanRenderer::createInstance() {
    uint32_t glfwExtCount = 0;
    const char** glfwExts =
        glfwGetRequiredInstanceExtensions(&glfwExtCount);

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "gpu-crash-provenance";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "provenance-page";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = glfwExtCount;
    ci.ppEnabledExtensionNames = glfwExts;
    check(vkCreateInstance(&ci, nullptr, &instance_), "vkCreateInstance");
}

void VulkanRenderer::createSurface() {
    check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
          "glfwCreateWindowSurface");
}

VulkanRenderer::QueueFamilies
VulkanRenderer::findQueueFamilies(VkPhysicalDevice dev) const {
    QueueFamilies qf;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, props.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) qf.graphics = i;
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &present);
        if (present) qf.present = i;
        if (qf.complete()) break;
    }
    return qf;
}

void VulkanRenderer::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    for (VkPhysicalDevice dev : devices) {
        QueueFamilies qf = findQueueFamilies(dev);
        if (!qf.complete()) continue;
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount,
                                             exts.data());
        bool hasSwapchain = std::any_of(
            exts.begin(), exts.end(), [](const VkExtensionProperties& e) {
                return std::strcmp(e.extensionName,
                                   VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            });
        if (!hasSwapchain) continue;
        uint32_t formatCount = 0, presentCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface_, &formatCount,
                                             nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface_, &presentCount,
                                                  nullptr);
        if (formatCount == 0 || presentCount == 0) continue;
        physicalDevice_ = dev;
        families_ = qf;
        return;
    }
    throw std::runtime_error("no suitable GPU with present support");
}

void VulkanRenderer::createLogicalDevice() {
    std::set<uint32_t> unique{families_.graphics, families_.present};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    float priority = 1.0f;
    for (uint32_t family : unique) {
        VkDeviceQueueCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex = family;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        queueInfos.push_back(q);
    }
    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount =
        static_cast<uint32_t>(queueInfos.size());
    ci.pQueueCreateInfos = queueInfos.data();
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = devExts;
    check(vkCreateDevice(physicalDevice_, &ci, nullptr, &device_),
          "vkCreateDevice");
    vkGetDeviceQueue(device_, families_.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, families_.present, 0, &presentQueue_);
}

void VulkanRenderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_,
                                              &caps);
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_,
                                         &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_,
                                         &formatCount, formats.data());
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    swapchainFormat_ = chosen.format;
    extent_ = caps.currentExtent;
    if (extent_.width == UINT32_MAX) {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        extent_.width = std::clamp(static_cast<uint32_t>(w),
                                   caps.minImageExtent.width,
                                   caps.maxImageExtent.width);
        extent_.height = std::clamp(static_cast<uint32_t>(h),
                                    caps.minImageExtent.height,
                                    caps.maxImageExtent.height);
    }
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface_;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    uint32_t qf[] = {families_.graphics, families_.present};
    if (families_.graphics == families_.present) {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = qf;
    }
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_),
          "vkCreateSwapchainKHR");

    uint32_t actual = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &actual, nullptr);
    std::vector<VkImage> images(actual);
    vkGetSwapchainImagesKHR(device_, swapchain_, &actual, images.data());
    frames_.resize(actual);
    for (uint32_t i = 0; i < actual; ++i) frames_[i].image = images[i];
}

void VulkanRenderer::createImageViews() {
    for (FrameData& f : frames_) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = f.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = swapchainFormat_;
        vi.components.r = vi.components.g = vi.components.b =
            vi.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.baseMipLevel = 0;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.baseArrayLayer = 0;
        vi.subresourceRange.layerCount = 1;
        check(vkCreateImageView(device_, &vi, nullptr, &f.view),
              "vkCreateImageView");
    }
}

void VulkanRenderer::createRenderPass() {
    VkAttachmentDescription color{};
    color.format = swapchainFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    check(vkCreateRenderPass(device_, &rp, nullptr, &renderPass_),
          "vkCreateRenderPass");
}

void VulkanRenderer::createFramebuffers() {
    for (FrameData& f : frames_) {
        VkImageView attachments[] = {f.view};
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = renderPass_;
        fb.attachmentCount = 1;
        fb.pAttachments = attachments;
        fb.width = extent_.width;
        fb.height = extent_.height;
        fb.layers = 1;
        check(vkCreateFramebuffer(device_, &fb, nullptr, &f.framebuffer),
              "vkCreateFramebuffer");
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = commandPool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device_, &ai, &f.cmd),
              "vkAllocateCommandBuffers");
    }
}

void VulkanRenderer::createCommandPoolAndBuffers() {
    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = families_.graphics;
    check(vkCreateCommandPool(device_, &pool, nullptr, &commandPool_),
          "vkCreateCommandPool");
}

void VulkanRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    check(vkCreateSemaphore(device_, &si, nullptr, &imageAvailable_),
          "semaphore");
    check(vkCreateSemaphore(device_, &si, nullptr, &renderFinished_),
          "semaphore");
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(device_, &fi, nullptr, &inFlight_), "fence");
}

void VulkanRenderer::destroySwapchainObjects() {
    // Command buffers are reclaimed together with the pool. On swapchain
    // recreation drawFrame() resets the pool; on teardown the pool is destroyed
    // after this function. Keeping the handles here avoids per-buffer frees,
    // which are also the least portable path across ICDs.
    for (FrameData& f : frames_) {
        if (f.framebuffer)
            vkDestroyFramebuffer(device_, f.framebuffer, nullptr);
        if (f.view) vkDestroyImageView(device_, f.view, nullptr);
    }
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
}

void VulkanRenderer::waitIdle() { vkDeviceWaitIdle(device_); }

void VulkanRenderer::drawFrame(DrawCallback callback) {
    vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX);
    uint32_t imageIndex = 0;
    VkResult acquired = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX, imageAvailable_, VK_NULL_HANDLE,
        &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        if (w == 0 || h == 0) return;
        vkDeviceWaitIdle(device_);
        destroySwapchainObjects();
        check(vkResetCommandPool(device_, commandPool_, 0),
              "vkResetCommandPool");
        frames_.clear();
        createSwapchain();
        createImageViews();
        createFramebuffers();
        return;
    }
    check(acquired, "vkAcquireNextImageKHR");
    vkResetFences(device_, 1, &inFlight_);

    VkCommandBuffer cmd = frames_[imageIndex].cmd;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");

    if (outsidePass_)
        outsidePass_(cmd, frames_[imageIndex].image, extent_);

    VkClearValue clear{};
    clear.color = {{clearColor_[0], clearColor_[1], clearColor_[2],
                    clearColor_[3]}};
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = renderPass_;
    rp.framebuffer = frames_[imageIndex].framebuffer;
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    if (callback) callback(cmd, extent_);
    vkCmdEndRenderPass(cmd);
    check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

    VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAvailable_;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished_;
    check(vkQueueSubmit(graphicsQueue_, 1, &submit, inFlight_),
          "vkQueueSubmit");

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex;
    VkResult presented = vkQueuePresentKHR(presentQueue_, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR ||
        presented == VK_SUBOPTIMAL_KHR ||
        glfwWindowShouldClose(window_)) {
        return;
    }
    check(presented, "vkQueuePresentKHR");
}

} // namespace gcdump::gui
