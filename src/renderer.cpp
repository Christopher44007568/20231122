#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "renderer.h"
#include "shaders_spv.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <sstream>

namespace ph {
namespace {

constexpr VkFormat kOffscreenFormat = VK_FORMAT_R8G8B8A8_UNORM;
const VkClearValue kClearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

struct PushConstants {
    float resolution[2];
    float scene_time_sec;
    float object_period_sec;
    float object_half_extent;
    uint32_t probe_count;
    uint32_t device_index;
    uint32_t reserved0;
    uint32_t reserved1;
};
static_assert(sizeof(PushConstants) == 36, "push constants must match scene.frag");

std::string vkError(const char* operation, VkResult result) {
    return std::string(operation) + " failed with VkResult " + std::to_string(result);
}

bool checked(VkResult result, const char* operation, std::string& error) {
    if (result == VK_SUCCESS) {
        return true;
    }
    error = vkError(operation, result);
    return false;
}

std::string versionText(uint32_t version) {
    std::ostringstream out;
    out << VK_VERSION_MAJOR(version) << "." << VK_VERSION_MINOR(version) << "."
        << VK_VERSION_PATCH(version);
    return out.str();
}

}  // namespace

VulkanRenderer::~VulkanRenderer() {
    destroy();
}

bool VulkanRenderer::initialize(int width, int height, GLFWwindow* window, std::string& error) {
    width_ = width;
    height_ = height;
    window_ = window;
    if (!createInstance(error)) {
        return false;
    }
    if (window_ != nullptr) {
        const VkResult surface_result =
            glfwCreateWindowSurface(instance_, window_, nullptr, &surface_);
        if (!checked(surface_result, "glfwCreateWindowSurface", error)) {
            return false;
        }
    }
    if (!pickPhysicalDevice(error) || !createDevice(error)) {
        return false;
    }
    if (surface_ != VK_NULL_HANDLE) {
        create_swapchain_ =
            reinterpret_cast<PFN_vkCreateSwapchainKHR>(
                vkGetInstanceProcAddr(instance_, "vkCreateSwapchainKHR"));
        destroy_swapchain_ =
            reinterpret_cast<PFN_vkDestroySwapchainKHR>(
                vkGetInstanceProcAddr(instance_, "vkDestroySwapchainKHR"));
        get_swapchain_images_ =
            reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
                vkGetInstanceProcAddr(instance_, "vkGetSwapchainImagesKHR"));
        acquire_next_image_ =
            reinterpret_cast<PFN_vkAcquireNextImageKHR>(
                vkGetInstanceProcAddr(instance_, "vkAcquireNextImageKHR"));
        queue_present_ =
            reinterpret_cast<PFN_vkQueuePresentKHR>(
                vkGetInstanceProcAddr(instance_, "vkQueuePresentKHR"));
        if (create_swapchain_ == nullptr || destroy_swapchain_ == nullptr ||
            get_swapchain_images_ == nullptr || acquire_next_image_ == nullptr ||
            queue_present_ == nullptr) {
            error = "failed to load swapchain entry points";
            return false;
        }
    }
    if (!createOffscreenTarget(error) || !createPipeline(error) || !createCommands(error)) {
        return false;
    }
    if (surface_ != VK_NULL_HANDLE) {
        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (!checked(vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available_),
                     "vkCreateSemaphore(image)", error) ||
            !checked(vkCreateSemaphore(device_, &semaphore_info, nullptr, &render_finished_),
                     "vkCreateSemaphore(render)", error) ||
            !createSwapchain(error)) {
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::createInstance(std::string& error) {
    uint32_t extension_count = 0;
    const char** glfw_extensions = nullptr;
    if (window_ != nullptr) {
        glfw_extensions = glfwGetRequiredInstanceExtensions(&extension_count);
        if (glfw_extensions == nullptr || extension_count == 0) {
            error = "GLFW did not provide Vulkan surface extensions";
            return false;
        }
    }

    std::vector<const char*> extensions;
    for (uint32_t i = 0; i < extension_count; ++i) {
        extensions.push_back(glfw_extensions[i]);
    }
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "pixel_history_sampler";
    application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application.pEngineName = "pixel_history_sampler";
    application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &application;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
    return checked(vkCreateInstance(&create_info, nullptr, &instance_), "vkCreateInstance", error);
}

bool VulkanRenderer::pickPhysicalDevice(std::string& error) {
    uint32_t count = 0;
    if (!checked(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
                 "vkEnumeratePhysicalDevices", error)) {
        return false;
    }
    if (count == 0) {
        error = "no Vulkan physical device found";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    if (!checked(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
                 "vkEnumeratePhysicalDevices", error)) {
        return false;
    }

    const auto surface_support =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
            vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceSurfaceSupportKHR"));
    int best_score = -1;
    for (VkPhysicalDevice candidate : devices) {
        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());
        for (uint32_t i = 0; i < family_count; ++i) {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                continue;
            }
            VkBool32 present = VK_FALSE;
            if (surface_ != VK_NULL_HANDLE) {
                if (surface_support == nullptr ||
                    surface_support(candidate, i, surface_, &present) != VK_SUCCESS ||
                    present == VK_FALSE) {
                    continue;
                }
            }
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score > best_score) {
                best_score = score;
                physical_device_ = candidate;
                queue_family_ = i;
                device_name_ = properties.deviceName;
                api_version_ = versionText(properties.apiVersion);
            }
        }
    }
    if (physical_device_ == VK_NULL_HANDLE) {
        error = "no Vulkan graphics/present queue family found";
        return false;
    }
    return true;
}

bool VulkanRenderer::createDevice(std::string& error) {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    std::vector<const char*> extensions;
    if (surface_ != VK_NULL_HANDLE) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
    if (!checked(vkCreateDevice(physical_device_, &create_info, nullptr, &device_),
                 "vkCreateDevice", error)) {
        return false;
    }
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    return true;
}

uint32_t VulkanRenderer::findMemoryType(uint32_t type_bits, VkMemoryPropertyFlags properties,
                                         bool& found) const {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) != 0 &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties) {
            found = true;
            return i;
        }
    }
    found = false;
    return 0;
}

VkShaderModule VulkanRenderer::createShaderModule(const uint32_t* words, size_t word_count,
                                                  std::string& error) const {
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = word_count * sizeof(uint32_t);
    create_info.pCode = words;
    VkShaderModule module = VK_NULL_HANDLE;
    if (!checked(vkCreateShaderModule(device_, &create_info, nullptr, &module),
                 "vkCreateShaderModule", error)) {
        return VK_NULL_HANDLE;
    }
    return module;

}

void VulkanRenderer::destroy() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    destroySwapchain();
    if (device_ != VK_NULL_HANDLE) {
        if (image_available_ != VK_NULL_HANDLE) vkDestroySemaphore(device_, image_available_, nullptr);
        if (render_finished_ != VK_NULL_HANDLE) vkDestroySemaphore(device_, render_finished_, nullptr);
        if (fence_ != VK_NULL_HANDLE) vkDestroyFence(device_, fence_, nullptr);
        if (command_pool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, command_pool_, nullptr);
        if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
        if (pipeline_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        if (framebuffer_ != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, framebuffer_, nullptr);
        if (render_pass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, render_pass_, nullptr);
        if (readback_buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, readback_buffer_, nullptr);
        if (readback_memory_ != VK_NULL_HANDLE) vkFreeMemory(device_, readback_memory_, nullptr);
        if (color_view_ != VK_NULL_HANDLE) vkDestroyImageView(device_, color_view_, nullptr);
        if (color_image_ != VK_NULL_HANDLE) vkDestroyImage(device_, color_image_, nullptr);
        if (color_memory_ != VK_NULL_HANDLE) vkFreeMemory(device_, color_memory_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
    instance_ = VK_NULL_HANDLE;
    surface_ = VK_NULL_HANDLE;
    physical_device_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    command_pool_ = VK_NULL_HANDLE;
    command_buffer_ = VK_NULL_HANDLE;
    fence_ = VK_NULL_HANDLE;
    color_image_ = VK_NULL_HANDLE;
    color_memory_ = VK_NULL_HANDLE;
    color_view_ = VK_NULL_HANDLE;
    color_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    readback_buffer_ = VK_NULL_HANDLE;
    readback_memory_ = VK_NULL_HANDLE;
    readback_mapped_ = nullptr;
    readback_coherent_ = false;
    render_pass_ = VK_NULL_HANDLE;
    framebuffer_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    swapchain_ = VK_NULL_HANDLE;
    image_available_ = VK_NULL_HANDLE;
    render_finished_ = VK_NULL_HANDLE;
}

bool VulkanRenderer::createSwapchain(std::string& error) {
    if (window_ == nullptr) {
        return true;
    }
    VkSurfaceCapabilitiesKHR capabilities{};
    if (!checked(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_,
                                                           &capabilities),
                 "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", error)) {
        return false;
    }

    uint32_t format_count = 0;
    if (!checked(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count,
                                                      nullptr),
                 "vkGetPhysicalDeviceSurfaceFormatsKHR", error)) {
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    if (format_count != 0 &&
        !checked(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count,
                                                      formats.data()),
                 "vkGetPhysicalDeviceSurfaceFormatsKHR", error)) {
        return false;
    }
    if (formats.empty()) {
        error = "Vulkan surface exposes no formats";
        return false;
    }

    VkSurfaceFormatKHR chosen = formats.front();
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = format;
            break;
        }
    }

    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);
    if (framebuffer_width <= 0 || framebuffer_height <= 0) {
        error = "GLFW framebuffer has zero extent";
        return false;
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max()) {
        extent.width = static_cast<uint32_t>(framebuffer_width);
        extent.height = static_cast<uint32_t>(framebuffer_height);
        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
    }

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface_;
    create_info.minImageCount = image_count;
    create_info.imageFormat = chosen.format;
    create_info.imageColorSpace = chosen.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    create_info.clipped = VK_TRUE;
    if (!checked(create_swapchain_(device_, &create_info, nullptr, &swapchain_),
                 "vkCreateSwapchainKHR", error)) {
        return false;
    }

    uint32_t actual_count = 0;
    if (!checked(get_swapchain_images_(device_, swapchain_, &actual_count, nullptr),
                 "vkGetSwapchainImagesKHR", error)) {
        return false;
    }
    swapchain_images_.resize(actual_count);
    if (!checked(get_swapchain_images_(device_, swapchain_, &actual_count,
                                       swapchain_images_.data()),
                 "vkGetSwapchainImagesKHR", error)) {
        return false;
    }
    swapchain_format_ = chosen.format;
    swapchain_extent_ = extent;
    swapchain_views_.resize(actual_count, VK_NULL_HANDLE);
    swapchain_layouts_.assign(actual_count, VK_IMAGE_LAYOUT_UNDEFINED);
    for (uint32_t i = 0; i < actual_count; ++i) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = swapchain_images_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain_format_;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        if (!checked(vkCreateImageView(device_, &view_info, nullptr, &swapchain_views_[i]),
                     "vkCreateImageView(swapchain)", error)) {
            return false;
        }
    }
    return true;
}

void VulkanRenderer::destroySwapchain() {
    if (device_ == VK_NULL_HANDLE) {
        swapchain_ = VK_NULL_HANDLE;
        return;
    }
    for (VkImageView view : swapchain_views_) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, view, nullptr);
        }
    }
    swapchain_views_.clear();
    swapchain_images_.clear();
    swapchain_layouts_.clear();
    if (swapchain_ != VK_NULL_HANDLE && destroy_swapchain_ != nullptr) {
        destroy_swapchain_(device_, swapchain_, nullptr);
    }
    swapchain_ = VK_NULL_HANDLE;
}

bool VulkanRenderer::createOffscreenTarget(std::string& error) {
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = kOffscreenFormat;
    image_info.extent = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!checked(vkCreateImage(device_, &image_info, nullptr, &color_image_),
                 "vkCreateImage", error)) {
        return false;
    }

    VkMemoryRequirements image_requirements{};
    vkGetImageMemoryRequirements(device_, color_image_, &image_requirements);
    bool found = false;
    uint32_t image_type = findMemoryType(image_requirements.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, found);
    if (!found) {
        image_type = findMemoryType(image_requirements.memoryTypeBits, 0, found);
    }
    if (!found) {
        error = "no compatible Vulkan memory type for offscreen image";
        return false;
    }
    VkMemoryAllocateInfo image_allocation{};
    image_allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    image_allocation.allocationSize = image_requirements.size;
    image_allocation.memoryTypeIndex = image_type;
    if (!checked(vkAllocateMemory(device_, &image_allocation, nullptr, &color_memory_),
                 "vkAllocateMemory(offscreen image)", error)) {
        return false;
    }
    if (!checked(vkBindImageMemory(device_, color_image_, color_memory_, 0),
                 "vkBindImageMemory", error)) {
        return false;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = color_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = kOffscreenFormat;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    if (!checked(vkCreateImageView(device_, &view_info, nullptr, &color_view_),
                 "vkCreateImageView(offscreen)", error)) {
        return false;
    }

    const VkDeviceSize readback_size =
        static_cast<VkDeviceSize>(width_) * static_cast<VkDeviceSize>(height_) * 4u;
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = readback_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!checked(vkCreateBuffer(device_, &buffer_info, nullptr, &readback_buffer_),
                 "vkCreateBuffer(readback)", error)) {
        return false;
    }
    VkMemoryRequirements buffer_requirements{};
    vkGetBufferMemoryRequirements(device_, readback_buffer_, &buffer_requirements);
    const VkMemoryPropertyFlags coherent =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t buffer_type = findMemoryType(buffer_requirements.memoryTypeBits, coherent, found);
    readback_coherent_ = found;
    if (!found) {
        buffer_type = findMemoryType(buffer_requirements.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, found);
    }
    if (!found) {
        error = "no host-visible Vulkan memory for readback";
        return false;
    }
    VkMemoryAllocateInfo buffer_allocation{};
    buffer_allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    buffer_allocation.allocationSize = buffer_requirements.size;
    buffer_allocation.memoryTypeIndex = buffer_type;
    if (!checked(vkAllocateMemory(device_, &buffer_allocation, nullptr, &readback_memory_),
                 "vkAllocateMemory(readback)", error)) {
        return false;
    }
    if (!checked(vkBindBufferMemory(device_, readback_buffer_, readback_memory_, 0),
                 "vkBindBufferMemory", error)) {
        return false;
    }
    if (!checked(vkMapMemory(device_, readback_memory_, 0, readback_size, 0, &readback_mapped_),
                 "vkMapMemory", error)) {
        return false;
    }

    VkAttachmentDescription attachment{};
    attachment.format = kOffscreenFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference color_reference{};
    color_reference.attachment = 0;
    color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_reference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;
    if (!checked(vkCreateRenderPass(device_, &render_pass_info, nullptr, &render_pass_),
                 "vkCreateRenderPass", error)) {
        return false;
    }

    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = render_pass_;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &color_view_;
    framebuffer_info.width = static_cast<uint32_t>(width_);
    framebuffer_info.height = static_cast<uint32_t>(height_);
    framebuffer_info.layers = 1;
    return checked(vkCreateFramebuffer(device_, &framebuffer_info, nullptr, &framebuffer_),
                   "vkCreateFramebuffer", error);
}

bool VulkanRenderer::createPipeline(std::string& error) {
    VkShaderModule vertex =
        createShaderModule(scene_vert_spv, sizeof(scene_vert_spv) / sizeof(uint32_t), error);
    if (vertex == VK_NULL_HANDLE) {
        return false;
    }
    VkShaderModule fragment =
        createShaderModule(scene_frag_spv, sizeof(scene_frag_spv) / sizeof(uint32_t), error);
    if (fragment == VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, vertex, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.width = static_cast<float>(width_);
    viewport.height = static_cast<float>(height_);
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_)}};
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_attachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend_attachment;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;
    if (!checked(vkCreatePipelineLayout(device_, &layout_info, nullptr, &pipeline_layout_),
                 "vkCreatePipelineLayout", error)) {
        vkDestroyShaderModule(device_, fragment, nullptr);
        vkDestroyShaderModule(device_, vertex, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDynamicState = &dynamic;
    pipeline_info.layout = pipeline_layout_;
    pipeline_info.renderPass = render_pass_;
    pipeline_info.subpass = 0;
    const VkResult result =
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, fragment, nullptr);
    vkDestroyShaderModule(device_, vertex, nullptr);
    return checked(result, "vkCreateGraphicsPipelines", error);
}

bool VulkanRenderer::createCommands(std::string& error) {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_;
    if (!checked(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_),
                 "vkCreateCommandPool", error)) {
        return false;
    }
    VkCommandBufferAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = command_pool_;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    if (!checked(vkAllocateCommandBuffers(device_, &allocation, &command_buffer_),
                 "vkAllocateCommandBuffers", error)) {
        return false;
    }
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    return checked(vkCreateFence(device_, &fence_info, nullptr, &fence_),
                   "vkCreateFence", error);
}

void VulkanRenderer::transitionImage(VkCommandBuffer command, VkImage image,
                                     VkImageLayout old_layout, VkImageLayout new_layout,
                                     VkAccessFlags src_access, VkAccessFlags dst_access,
                                     VkPipelineStageFlags src_stage,
                                     VkPipelineStageFlags dst_stage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    vkCmdPipelineBarrier(command, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool VulkanRenderer::recordAndSubmitRender(int64_t scene_tick_us, const ClockConfig& config,
                                           uint32_t device_index, std::string& error) {
    if (!checked(vkResetCommandBuffer(command_buffer_, 0), "vkResetCommandBuffer", error) ||
        !checked(vkResetFences(device_, 1, &fence_), "vkResetFences", error)) {
        return false;
    }
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!checked(vkBeginCommandBuffer(command_buffer_, &begin_info),
                 "vkBeginCommandBuffer", error)) {
        return false;
    }
    transitionImage(command_buffer_, color_image_, color_layout_,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderPassBeginInfo render_begin{};
    render_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_begin.renderPass = render_pass_;
    render_begin.framebuffer = framebuffer_;
    render_begin.renderArea = {{0, 0}, {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_)}};
    render_begin.clearValueCount = 1;
    render_begin.pClearValues = &kClearColor;
    vkCmdBeginRenderPass(command_buffer_, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{};
    viewport.width = static_cast<float>(width_);
    viewport.height = static_cast<float>(height_);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(command_buffer_, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_)}};
    vkCmdSetScissor(command_buffer_, 0, 1, &scissor);

    PushConstants constants{};
    constants.resolution[0] = static_cast<float>(width_);
    constants.resolution[1] = static_cast<float>(height_);
    constants.scene_time_sec = static_cast<float>(scene_tick_us) / 1000000.0f;
    constants.object_period_sec = static_cast<float>(config.object_period_us) / 1000000.0f;
    constants.object_half_extent = config.object_half_extent;
    constants.probe_count = static_cast<uint32_t>(config.probe_count);
    constants.device_index = device_index;
    vkCmdPushConstants(command_buffer_, pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(constants), &constants);
    vkCmdDraw(command_buffer_, 3, 1, 0, 0);
    vkCmdEndRenderPass(command_buffer_);

    transitionImage(command_buffer_, color_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};
    vkCmdCopyImageToBuffer(command_buffer_, color_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback_buffer_, 1, &copy);
    if (!checked(vkEndCommandBuffer(command_buffer_), "vkEndCommandBuffer", error)) {
        return false;
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer_;
    if (!checked(vkQueueSubmit(queue_, 1, &submit, fence_), "vkQueueSubmit(render)", error) ||
        !checked(vkWaitForFences(device_, 1, &fence_, VK_TRUE,
                                 std::numeric_limits<uint64_t>::max()),
                 "vkWaitForFences(render)", error)) {
        return false;
    }
    color_layout_ = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return true;
}

bool VulkanRenderer::renderFrame(int64_t scene_tick_us, const ClockConfig& config,
                                 uint32_t device_index, std::vector<uint8_t>& rgba,
                                 std::string& error) {
    if (!recordAndSubmitRender(scene_tick_us, config, device_index, error)) {
        return false;
    }
    const VkDeviceSize size =
        static_cast<VkDeviceSize>(width_) * static_cast<VkDeviceSize>(height_) * 4u;
    if (!readback_coherent_) {
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = readback_memory_;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        if (!checked(vkInvalidateMappedMemoryRanges(device_, 1, &range),
                     "vkInvalidateMappedMemoryRanges", error)) {
            return false;
        }
    }
    rgba.resize(static_cast<size_t>(size));
    std::memcpy(rgba.data(), readback_mapped_, static_cast<size_t>(size));
    return true;
}

bool VulkanRenderer::present(std::string& error) {
    if (surface_ == VK_NULL_HANDLE) {
        return true;
    }
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);
    if (framebuffer_width <= 0 || framebuffer_height <= 0) {
        return true;
    }
    if (swapchain_ == VK_NULL_HANDLE ||
        static_cast<uint32_t>(framebuffer_width) != swapchain_extent_.width ||
        static_cast<uint32_t>(framebuffer_height) != swapchain_extent_.height) {
        vkDeviceWaitIdle(device_);
        destroySwapchain();
        if (!createSwapchain(error)) {
            return false;
        }
    }

    uint32_t image_index = 0;
    VkResult acquire = acquire_next_image_(device_, swapchain_,
                                           std::numeric_limits<uint64_t>::max(),
                                           image_available_, VK_NULL_HANDLE, &image_index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDeviceWaitIdle(device_);
        destroySwapchain();
        if (!createSwapchain(error)) {
            return false;
        }
        acquire = acquire_next_image_(device_, swapchain_,
                                      std::numeric_limits<uint64_t>::max(),
                                      image_available_, VK_NULL_HANDLE, &image_index);
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        error = vkError("vkAcquireNextImageKHR", acquire);
        return false;
    }

    if (!checked(vkResetCommandBuffer(command_buffer_, 0), "vkResetCommandBuffer(present)",
                 error) ||
        !checked(vkResetFences(device_, 1, &fence_), "vkResetFences(present)", error)) {
        return false;
    }
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!checked(vkBeginCommandBuffer(command_buffer_, &begin_info),
                 "vkBeginCommandBuffer(present)", error)) {
        return false;
    }

    transitionImage(command_buffer_, swapchain_images_[image_index],
                    swapchain_layouts_[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1] = {width_, height_, 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[1] = {static_cast<int32_t>(swapchain_extent_.width),
                          static_cast<int32_t>(swapchain_extent_.height), 1};
    vkCmdBlitImage(command_buffer_, color_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchain_images_[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &blit, VK_FILTER_NEAREST);
    transitionImage(command_buffer_, swapchain_images_[image_index],
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    if (!checked(vkEndCommandBuffer(command_buffer_), "vkEndCommandBuffer(present)", error)) {
        return false;
    }

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &image_available_;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_finished_;
    if (!checked(vkQueueSubmit(queue_, 1, &submit, fence_), "vkQueueSubmit(present)", error) ||
        !checked(vkWaitForFences(device_, 1, &fence_, VK_TRUE,
                                 std::numeric_limits<uint64_t>::max()),
                 "vkWaitForFences(present)", error)) {
        return false;
    }

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished_;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_;
    present_info.pImageIndices = &image_index;
    const VkResult present_result = queue_present_(queue_, &present_info);
    swapchain_layouts_[image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
        vkDeviceWaitIdle(device_);
        destroySwapchain();
        if (!createSwapchain(error)) {
            return false;
        }
        return true;
    }
    if (present_result != VK_SUCCESS) {
        error = vkError("vkQueuePresentKHR", present_result);
        return false;
    }
    return true;
}

}  // namespace ph
