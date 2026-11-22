#include "skincheck/gui.hpp"

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <vulkan/vulkan.h>

#include "skincheck/json.hpp"

namespace skincheck {
namespace {

void vk_check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(static_cast<int>(result)));
    }
}

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path);
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

struct RunView {
    std::string status = "unknown";
    std::string mode = "unknown";
    std::string phase = "unknown";
    std::vector<std::string> frame_statuses;
};

RunView load_run_view(const std::string& directory) {
    std::string error;
    const Json root = Json::parse(read_file(directory + "/run.json"), &error);
    if (!error.empty() || !root.is_object()) {
        throw std::runtime_error("cannot parse run.json: " + error);
    }
    RunView view;
    if (const Json* summary = root.find("summary"); summary != nullptr) {
        if (const Json* status = summary->find("status"); status != nullptr) {
            view.status = status->as_string();
        }
    }
    if (const Json* resource = root.find("resource"); resource != nullptr) {
        if (const Json* mode = resource->find("mode"); mode != nullptr) {
            view.mode = mode->as_string();
        }
    }
    if (const Json* phase = root.find("phase"); phase != nullptr) {
        view.phase = phase->as_string();
    }
    if (const Json* frames = root.find("frames"); frames != nullptr && frames->is_array()) {
        for (const Json& frame : frames->items()) {
            const Json* status = frame.find("status");
            view.frame_statuses.push_back(status != nullptr ? status->as_string() : "unknown");
        }
    }
    return view;
}

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;

    bool complete() const { return graphics.has_value() && present.has_value(); }
};

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

std::array<float, 4> status_color(const std::string& status) {
    if (status == "pass") return {0.10F, 0.64F, 0.30F, 1.0F};
    if (status == "pass_degraded") return {0.15F, 0.42F, 0.82F, 1.0F};
    if (status == "fail") return {0.82F, 0.12F, 0.14F, 1.0F};
    if (status == "fail_degraded") return {0.88F, 0.30F, 0.08F, 1.0F};
    if (status == "fail_incomplete") return {0.88F, 0.52F, 0.05F, 1.0F};
    if (status == "incomplete") return {0.92F, 0.74F, 0.12F, 1.0F};
    return {0.45F, 0.45F, 0.50F, 1.0F};
}

class Viewer {
public:
    explicit Viewer(RunView view) : view_(std::move(view)) {}
    ~Viewer() { cleanup(); }

    void run() {
        initialize();
        main_loop();
    }

private:
    RunView view_;
    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
    std::uint32_t present_family_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkSemaphore image_available_ = VK_NULL_HANDLE;
    VkSemaphore render_finished_ = VK_NULL_HANDLE;
    VkFence in_flight_ = VK_NULL_HANDLE;

    void initialize() {
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        if (!glfwVulkanSupported()) {
            throw std::runtime_error("GLFW reports no Vulkan loader support");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        window_ = glfwCreateWindow(1100, 360, "skincheck", nullptr, nullptr);
        if (window_ == nullptr) throw std::runtime_error("GLFW window creation failed");
        const std::string title = "skincheck | phase=" + view_.phase + " | status=" + view_.status +
                                  " | frames=" + std::to_string(view_.frame_statuses.size()) +
                                  " | mode=" + view_.mode + " | Esc closes";
        glfwSetWindowTitle(window_, title.c_str());

        create_instance();
        create_surface();
        pick_physical_device();
        create_device();
        create_swapchain();
        create_render_pass();
        create_framebuffers();
        create_commands();
        create_sync();
        log_memory_heaps();
    }

    void create_instance() {
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "skincheck";
        application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        application.pEngineName = "skincheck-core";
        application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        application.apiVersion = VK_API_VERSION_1_1;

        std::uint32_t extension_count = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extension_count);
        if (extensions == nullptr || extension_count == 0) {
            throw std::runtime_error("GLFW did not provide Vulkan surface extensions");
        }
        VkInstanceCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create.pApplicationInfo = &application;
        create.enabledExtensionCount = extension_count;
        create.ppEnabledExtensionNames = extensions;
        vk_check(vkCreateInstance(&create, nullptr, &instance_), "vkCreateInstance");
    }

    void create_surface() {
        vk_check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
                 "glfwCreateWindowSurface");
    }

    QueueFamilies queue_families(VkPhysicalDevice device) const {
        QueueFamilies families;
        std::uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> properties(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
        for (std::uint32_t i = 0; i < count; ++i) {
            if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                families.graphics = i;
            }
            VkBool32 present_supported = VK_FALSE;
            vk_check(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &present_supported),
                     "vkGetPhysicalDeviceSurfaceSupportKHR");
            if (present_supported == VK_TRUE) families.present = i;
            if (families.complete()) break;
        }
        return families;
    }

    SwapchainSupport swapchain_support(VkPhysicalDevice device) const {
        SwapchainSupport support;
        vk_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_,
                                                           &support.capabilities),
                 "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        std::uint32_t count = 0;
        vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &count, nullptr),
                 "vkGetPhysicalDeviceSurfaceFormatsKHR");
        support.formats.resize(count);
        vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &count,
                                                      support.formats.data()),
                 "vkGetPhysicalDeviceSurfaceFormatsKHR");
        vk_check(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &count, nullptr),
                 "vkGetPhysicalDeviceSurfacePresentModesKHR");
        support.present_modes.resize(count);
        vk_check(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &count,
                                                           support.present_modes.data()),
                 "vkGetPhysicalDeviceSurfacePresentModesKHR");
        return support;
    }

    bool suitable(VkPhysicalDevice device) const {
        std::uint32_t extension_count = 0;
        vk_check(vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr),
                 "vkEnumerateDeviceExtensionProperties");
        std::vector<VkExtensionProperties> extensions(extension_count);
        vk_check(vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count,
                                                      extensions.data()),
                 "vkEnumerateDeviceExtensionProperties");
        bool swapchain = false;
        for (const auto& extension : extensions) {
            swapchain = swapchain ||
                        std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
        }
        if (!swapchain || !queue_families(device).complete()) return false;
        const SwapchainSupport support = swapchain_support(device);
        return !support.formats.empty() && !support.present_modes.empty();
    }

    void pick_physical_device() {
        std::uint32_t count = 0;
        vk_check(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
                 "vkEnumeratePhysicalDevices");
        if (count == 0) throw std::runtime_error("no Vulkan physical devices");
        std::vector<VkPhysicalDevice> devices(count);
        vk_check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
                 "vkEnumeratePhysicalDevices");
        for (VkPhysicalDevice device : devices) {
            if (suitable(device)) {
                physical_device_ = device;
                break;
            }
        }
        if (physical_device_ == VK_NULL_HANDLE) {
            throw std::runtime_error("no Vulkan device supports graphics and presentation");
        }
    }

    void create_device() {
        const QueueFamilies families = queue_families(physical_device_);
        queue_family_ = *families.graphics;
        present_family_ = *families.present;
        const float priority = 1.0F;
        std::set<std::uint32_t> unique_families = {queue_family_, present_family_};
        std::vector<VkDeviceQueueCreateInfo> queues;
        for (std::uint32_t family : unique_families) {
            VkDeviceQueueCreateInfo queue{};
            queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue.queueFamilyIndex = family;
            queue.queueCount = 1;
            queue.pQueuePriorities = &priority;
            queues.push_back(queue);
        }
        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create.queueCreateInfoCount = static_cast<std::uint32_t>(queues.size());
        create.pQueueCreateInfos = queues.data();
        create.enabledExtensionCount = 1;
        create.ppEnabledExtensionNames = extensions;
        vk_check(vkCreateDevice(physical_device_, &create, nullptr, &device_),
                 "vkCreateDevice");
        vkGetDeviceQueue(device_, queue_family_, 0, &graphics_queue_);
        vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);
    }

    VkSurfaceFormatKHR choose_format(const std::vector<VkSurfaceFormatKHR>& formats) const {
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }
        return formats.front();
    }

    VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes) const {
        for (const VkPresentModeKHR mode : modes) {
            if (mode == VK_PRESENT_MODE_FIFO_KHR) return mode;
        }
        return modes.front();
    }

    VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities) const {
        if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        VkExtent2D extent{};
        extent.width = static_cast<std::uint32_t>(
            std::clamp(width, static_cast<int>(capabilities.minImageExtent.width),
                       static_cast<int>(capabilities.maxImageExtent.width)));
        extent.height = static_cast<std::uint32_t>(
            std::clamp(height, static_cast<int>(capabilities.minImageExtent.height),
                       static_cast<int>(capabilities.maxImageExtent.height)));
        return extent;
    }

    void create_swapchain() {
        const SwapchainSupport support = swapchain_support(physical_device_);
        const VkSurfaceFormatKHR format = choose_format(support.formats);
        const VkPresentModeKHR present_mode = choose_present_mode(support.present_modes);
        extent_ = choose_extent(support.capabilities);
        swapchain_format_ = format.format;

        std::uint32_t image_count = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 &&
            image_count > support.capabilities.maxImageCount) {
            image_count = support.capabilities.maxImageCount;
        }
        VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if ((support.capabilities.supportedCompositeAlpha & composite) == 0) {
            composite = static_cast<VkCompositeAlphaFlagBitsKHR>(
                support.capabilities.supportedCompositeAlpha & (~0U));
        }
        VkSwapchainCreateInfoKHR create{};
        create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create.surface = surface_;
        create.minImageCount = image_count;
        create.imageFormat = format.format;
        create.imageColorSpace = format.colorSpace;
        create.imageExtent = extent_;
        create.imageArrayLayers = 1;
        create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const std::uint32_t indices[] = {queue_family_, present_family_};
        if (queue_family_ != present_family_) {
            create.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            create.queueFamilyIndexCount = 2;
            create.pQueueFamilyIndices = indices;
        } else {
            create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        create.preTransform = support.capabilities.currentTransform;
        create.compositeAlpha = composite;
        create.presentMode = present_mode;
        create.clipped = VK_TRUE;
        vk_check(vkCreateSwapchainKHR(device_, &create, nullptr, &swapchain_),
                 "vkCreateSwapchainKHR");
        vk_check(vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr),
                 "vkGetSwapchainImagesKHR");
        images_.resize(image_count);
        vk_check(vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, images_.data()),
                 "vkGetSwapchainImagesKHR");

        image_views_.resize(images_.size());
        for (std::size_t i = 0; i < images_.size(); ++i) {
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = images_[i];
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = swapchain_format_;
            view.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                               VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.levelCount = 1;
            view.subresourceRange.layerCount = 1;
            vk_check(vkCreateImageView(device_, &view, nullptr, &image_views_[i]),
                     "vkCreateImageView");
        }
    }

    void create_render_pass() {
        VkAttachmentDescription color{};
        color.format = swapchain_format_;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference reference{};
        reference.attachment = 0;
        reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        create.attachmentCount = 1;
        create.pAttachments = &color;
        create.subpassCount = 1;
        create.pSubpasses = &subpass;
        create.dependencyCount = 1;
        create.pDependencies = &dependency;
        vk_check(vkCreateRenderPass(device_, &create, nullptr, &render_pass_),
                 "vkCreateRenderPass");
    }

    void create_framebuffers() {
        framebuffers_.resize(image_views_.size());
        for (std::size_t i = 0; i < image_views_.size(); ++i) {
            VkImageView attachment = image_views_[i];
            VkFramebufferCreateInfo create{};
            create.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            create.renderPass = render_pass_;
            create.attachmentCount = 1;
            create.pAttachments = &attachment;
            create.width = extent_.width;
            create.height = extent_.height;
            create.layers = 1;
            vk_check(vkCreateFramebuffer(device_, &create, nullptr, &framebuffers_[i]),
                     "vkCreateFramebuffer");
        }
    }

    void create_commands() {
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = queue_family_;
        vk_check(vkCreateCommandPool(device_, &pool, nullptr, &command_pool_),
                 "vkCreateCommandPool");
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = command_pool_;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        vk_check(vkAllocateCommandBuffers(device_, &allocate, &command_buffer_),
                 "vkAllocateCommandBuffers");
    }

    void create_sync() {
        VkSemaphoreCreateInfo semaphore{};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fence{};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vk_check(vkCreateSemaphore(device_, &semaphore, nullptr, &image_available_),
                 "vkCreateSemaphore");
        vk_check(vkCreateSemaphore(device_, &semaphore, nullptr, &render_finished_),
                 "vkCreateSemaphore");
        vk_check(vkCreateFence(device_, &fence, nullptr, &in_flight_), "vkCreateFence");
    }

    void log_memory_heaps() const {
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory);
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device_, &properties);
        std::cout << "Vulkan device: " << properties.deviceName << "\n";
        for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
            const auto& heap = memory.memoryHeaps[i];
            std::cout << "  heap " << i << ": " << (heap.size / (1024 * 1024))
                      << " MiB"
                      << ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0 ? " device-local"
                                                                              : " host")
                      << "\n";
        }
    }

    void record_commands(std::uint32_t image_index) {
        vk_check(vkResetCommandBuffer(command_buffer_, 0), "vkResetCommandBuffer");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vk_check(vkBeginCommandBuffer(command_buffer_, &begin), "vkBeginCommandBuffer");

        VkClearValue background{};
        background.color = {{0.035F, 0.040F, 0.055F, 1.0F}};
        VkRenderPassBeginInfo pass{};
        pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass = render_pass_;
        pass.framebuffer = framebuffers_[image_index];
        pass.renderArea.offset = {0, 0};
        pass.renderArea.extent = extent_;
        pass.clearValueCount = 1;
        pass.pClearValues = &background;
        vkCmdBeginRenderPass(command_buffer_, &pass, VK_SUBPASS_CONTENTS_INLINE);

        if (!view_.frame_statuses.empty()) {
            const int margin = 8;
            const int usable_width =
                std::max(1, static_cast<int>(extent_.width) - margin * 2);
            const int usable_height =
                std::max(1, static_cast<int>(extent_.height) - margin * 2);
            const int frame_count = static_cast<int>(view_.frame_statuses.size());
            for (int i = 0; i < frame_count; ++i) {
                const int x0 = margin + (usable_width * i) / frame_count;
                const int x1 = margin + (usable_width * (i + 1)) / frame_count;
                VkClearRect rect{};
                rect.rect.offset = {x0, margin};
                rect.rect.extent = {static_cast<std::uint32_t>(std::max(1, x1 - x0 - 2)),
                                    static_cast<std::uint32_t>(usable_height)};
                rect.baseArrayLayer = 0;
                rect.layerCount = 1;
                VkClearAttachment clear{};
                clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                clear.colorAttachment = 0;
                const auto color = status_color(view_.frame_statuses[static_cast<std::size_t>(i)]);
                clear.clearValue.color = {{color[0], color[1], color[2], color[3]}};
                vkCmdClearAttachments(command_buffer_, 1, &clear, 1, &rect);
            }
        }
        vkCmdEndRenderPass(command_buffer_);
        vk_check(vkEndCommandBuffer(command_buffer_), "vkEndCommandBuffer");
    }

    void draw_frame() {
        vk_check(vkWaitForFences(device_, 1, &in_flight_, VK_TRUE, UINT64_MAX),
                 "vkWaitForFences");
        std::uint32_t image_index = 0;
        const VkResult acquire =
            vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_available_,
                                  VK_NULL_HANDLE, &image_index);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) return;
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
            vk_check(acquire, "vkAcquireNextImageKHR");
        }
        vk_check(vkResetFences(device_, 1, &in_flight_), "vkResetFences");
        record_commands(image_index);

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &image_available_;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command_buffer_;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &render_finished_;
        vk_check(vkQueueSubmit(graphics_queue_, 1, &submit, in_flight_), "vkQueueSubmit");

        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &render_finished_;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &image_index;
        const VkResult result = vkQueuePresentKHR(present_queue_, &present);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR &&
            result != VK_ERROR_OUT_OF_DATE_KHR) {
            vk_check(result, "vkQueuePresentKHR");
        }
        vkQueueWaitIdle(present_queue_);
    }

    void main_loop() {
        while (glfwWindowShouldClose(window_) == 0) {
            glfwPollEvents();
            if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwSetWindowShouldClose(window_, GLFW_TRUE);
            }
            draw_frame();
        }
        vkDeviceWaitIdle(device_);
    }

    void cleanup() {
        if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
        if (device_ != VK_NULL_HANDLE) {
            if (in_flight_ != VK_NULL_HANDLE) vkDestroyFence(device_, in_flight_, nullptr);
            if (render_finished_ != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, render_finished_, nullptr);
            }
            if (image_available_ != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, image_available_, nullptr);
            }
            if (command_pool_ != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_, command_pool_, nullptr);
            }
            for (VkFramebuffer framebuffer : framebuffers_) {
                vkDestroyFramebuffer(device_, framebuffer, nullptr);
            }
            if (render_pass_ != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device_, render_pass_, nullptr);
            }
            for (VkImageView view : image_views_) vkDestroyImageView(device_, view, nullptr);
            if (swapchain_ != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            }
            vkDestroyDevice(device_, nullptr);
        }
        if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
        if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
        if (window_ != nullptr) glfwDestroyWindow(window_);
        glfwTerminate();
    }
};

}  // namespace

int run_gui(const std::string& run_directory) {
    try {
        const RunView view = load_run_view(run_directory);
        Viewer viewer(view);
        viewer.run();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "gui error: " << exception.what() << "\n";
        return 1;
    }
}

}  // namespace skincheck
