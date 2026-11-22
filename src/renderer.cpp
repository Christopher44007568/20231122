#include "renderer.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

constexpr uint64_t MiB = 1024ull * 1024ull;

std::string vkResultName(VkResult result)
{
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
    }
}

const char* deviceTypeName(VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated_gpu";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete_gpu";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual_gpu";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
    default: return "other";
    }
}

VkExtent2D clampExtent(VkExtent2D desired, const VkSurfaceCapabilitiesKHR& caps)
{
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return caps.currentExtent;
    }
    VkExtent2D extent = desired;
    extent.width = std::clamp(extent.width, caps.minImageExtent.width,
                              caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height,
                               caps.maxImageExtent.height);
    return extent;
}

int deviceScore(VkPhysicalDevice device, uint32_t queueFamily,
                VkSurfaceKHR surface)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    uint32_t formatCount = 0;
    uint32_t presentCount = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount,
                                             nullptr) != VK_SUCCESS ||
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface,
                                                   &presentCount,
                                                   nullptr) != VK_SUCCESS ||
        formatCount == 0 || presentCount == 0) {
        return -1;
    }
    int score = 100;
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 3000;
    } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score += 1000;
    }
    score += static_cast<int>(properties.limits.maxComputeWorkGroupInvocations) / 16;
    (void)queueFamily;
    return score;
}

} // namespace

VulkanFogRenderer::VulkanFogRenderer(FogConfig config, RunOptions options)
    : config_(std::move(config)), options_(std::move(options))
{
}

VulkanFogRenderer::~VulkanFogRenderer()
{
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    destroyVolumeResources();
    destroySwapchainDependent();

    if (device_ != VK_NULL_HANDLE) {
        if (fogSampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, fogSampler_, nullptr);
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        }
        if (uniformBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, uniformBuffer_, nullptr);
        if (uniformMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, uniformMemory_, nullptr);
        if (statsBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, statsBuffer_, nullptr);
        if (statsMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, statsMemory_, nullptr);
        if (computePipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, computePipeline_, nullptr);
        }
        if (displayPipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, displayPipeline_, nullptr);
        }
        if (computePipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, computePipelineLayout_, nullptr);
        }
        if (displayPipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, displayPipelineLayout_, nullptr);
        }
        if (computeSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, computeSetLayout_, nullptr);
        }
        if (displaySetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, displaySetLayout_, nullptr);
        }
        if (fogShader_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, fogShader_, nullptr);
        if (displayVertShader_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, displayVertShader_, nullptr);
        }
        if (displayFragShader_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, displayFragShader_, nullptr);
        }
        if (frameFence_ != VK_NULL_HANDLE) vkDestroyFence(device_, frameFence_, nullptr);
        if (imageAvailable_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, imageAvailable_, nullptr);
        }
        if (renderFinished_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, renderFinished_, nullptr);
        }
        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
        }
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
    }
    if (glfwReady_) {
        glfwTerminate();
    }
}

void VulkanFogRenderer::initialize()
{
    createWindow();
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createDevice();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily_;
    checkResult(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_),
                "create command pool");

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    checkResult(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer_),
                "allocate command buffer");

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    checkResult(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailable_),
                "create image semaphore");
    checkResult(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinished_),
                "create render semaphore");
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    checkResult(vkCreateFence(device_, &fenceInfo, nullptr, &frameFence_),
                "create frame fence");

    createSwapchain();
    createSwapchainImageViews();
    createRenderPass();
    createFramebuffers();

    createShaderModules();
    createDescriptorLayouts();
    createUniformAndStatsBuffers();
    createDescriptorPool();
    createPipelines();

    const uint64_t simulatedBudget = options_.simulatedVramMb > 0
        ? static_cast<uint64_t>(options_.simulatedVramMb) * MiB
        : 0;
    memoryBudgetBytes_ = simulatedBudget > 0
        ? simulatedBudget
        : std::max(deviceMemoryBytes_ * 65ull / 100ull, 128ull * MiB);
    const bool simulated = simulatedBudget > 0;
    plan_ = chooseMemoryPlan(config_, memoryBudgetBytes_, simulated);

    bool resourcesReady = false;
    std::string lastError;
    for (int tier = plan_.tier; tier <= 2; ++tier) {
        MemoryPlan candidate = memoryPlanForTier(config_, tier);
        if (tier == plan_.tier) {
            candidate.degradations = plan_.degradations;
        } else {
            std::ostringstream reason;
            reason << "allocation retry after resource failure: tier "
                   << plan_.tier << " -> " << tier << " (" << lastError << ")";
            candidate.degradations = plan_.degradations;
            candidate.degradations.push_back(reason.str());
        }
        plan_ = candidate;
        try {
            createVolumeResources();
            resourcesReady = true;
            break;
        } catch (const std::exception& error) {
            lastError = error.what();
            destroyVolumeResources();
            warnings_.push_back(lastError);
        }
    }
    if (!resourcesReady) {
        throw std::runtime_error("unable to allocate degraded fog resources: " +
                                 lastError);
    }
    initialized_ = true;
}

void VulkanFogRenderer::createWindow()
{
    if (!glfwInit()) {
        throw std::runtime_error("glfwInit failed");
    }
    glfwReady_ = true;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, options_.headless ? GLFW_FALSE : GLFW_TRUE);
    window_ = glfwCreateWindow(960, 540, "Stable Volumetric Fog", nullptr, nullptr);
    if (window_ == nullptr) {
        throw std::runtime_error("glfwCreateWindow failed");
    }
}

void VulkanFogRenderer::createInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "fog_stability";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "none";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (glfwExtensions == nullptr || glfwExtensionCount == 0) {
        throw std::runtime_error("GLFW did not provide Vulkan extensions");
    }
    std::vector<const char*> extensions(glfwExtensions,
                                        glfwExtensions + glfwExtensionCount);

    std::vector<const char*> layers;
    if (options_.validation) {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> available(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, available.data());
        for (const auto& layer : available) {
            if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                layers.push_back("VK_LAYER_KHRONOS_validation");
                break;
            }
        }
        if (layers.empty()) {
            warnings_.push_back("validation layer requested but unavailable");
        }
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
    checkResult(vkCreateInstance(&createInfo, nullptr, &instance_),
                "create Vulkan instance");
}

void VulkanFogRenderer::createSurface()
{
    checkResult(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
                "create GLFW Vulkan surface");
}

void VulkanFogRenderer::pickPhysicalDevice()
{
    uint32_t count = 0;
    checkResult(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
                "enumerate physical devices");
    if (count == 0) {
        throw std::runtime_error("no Vulkan physical devices");
    }
    std::vector<VkPhysicalDevice> devices(count);
    checkResult(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
                "read physical devices");

    VkPhysicalDevice best = VK_NULL_HANDLE;
    uint32_t bestFamily = 0;
    int bestScore = -1;
    for (VkPhysicalDevice device : devices) {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount,
                                                 families.data());
        for (uint32_t family = 0; family < familyCount; ++family) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, family, surface_,
                                                 &present);
            const VkQueueFlags required =
                VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if (!present || (families[family].queueFlags & required) != required) {
                continue;
            }
            const int score = deviceScore(device, family, surface_);
            if (score > bestScore) {
                best = device;
                bestFamily = family;
                bestScore = score;
            }
            break;
        }
    }
    if (best == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "no Vulkan device with graphics+compute+present queue");
    }
    physicalDevice_ = best;
    queueFamily_ = bestFamily;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    deviceName_ = properties.deviceName;
    deviceType_ = deviceTypeName(properties.deviceType);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties_);
    deviceMemoryBytes_ = 0;
    for (uint32_t index = 0; index < memoryProperties_.memoryHeapCount; ++index) {
        if ((memoryProperties_.memoryHeaps[index].flags &
             VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
            deviceMemoryBytes_ += memoryProperties_.memoryHeaps[index].size;
        }
    }
}

void VulkanFogRenderer::createDevice()
{
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;
    checkResult(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
                "create logical device");
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
}

void VulkanFogRenderer::createSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities{};
    checkResult(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                    physicalDevice_, surface_, &capabilities),
                "query surface capabilities");

    uint32_t formatCount = 0;
    checkResult(vkGetPhysicalDeviceSurfaceFormatsKHR(
                    physicalDevice_, surface_, &formatCount, nullptr),
                "count surface formats");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    checkResult(vkGetPhysicalDeviceSurfaceFormatsKHR(
                    physicalDevice_, surface_, &formatCount, formats.data()),
                "read surface formats");

    VkSurfaceFormatKHR selectedFormat = formats.front();
    for (const auto& format : formats) {
        if ((format.format == VK_FORMAT_B8G8R8A8_UNORM ||
             format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            selectedFormat = format;
            break;
        }
    }
    swapchainFormat_ = selectedFormat.format;

    uint32_t presentCount = 0;
    checkResult(vkGetPhysicalDeviceSurfacePresentModesKHR(
                    physicalDevice_, surface_, &presentCount, nullptr),
                "count present modes");
    std::vector<VkPresentModeKHR> presentModes(presentCount);
    checkResult(vkGetPhysicalDeviceSurfacePresentModesKHR(
                    physicalDevice_, surface_, &presentCount, presentModes.data()),
                "read present modes");
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR candidate : presentModes) {
        if (candidate == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode = candidate;
            break;
        }
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    VkExtent2D desired{static_cast<uint32_t>(std::max(width, 1)),
                       static_cast<uint32_t>(std::max(height, 1))};
    swapchainExtent_ = clampExtent(desired, capabilities);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 &&
        imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((capabilities.supportedCompositeAlpha &
         VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == 0) {
        composite = static_cast<VkCompositeAlphaFlagBitsKHR>(
            capabilities.supportedCompositeAlpha & 0x1u);
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = selectedFormat.format;
    createInfo.imageColorSpace = selectedFormat.colorSpace;
    createInfo.imageExtent = swapchainExtent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = composite;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    checkResult(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_),
                "create swapchain");

    checkResult(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr),
                "count swapchain images");
    swapchainImages_.resize(imageCount);
    checkResult(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount,
                                        swapchainImages_.data()),
                "read swapchain images");
}

void VulkanFogRenderer::createSwapchainImageViews()
{
    swapchainImageViews_.resize(swapchainImages_.size(), VK_NULL_HANDLE);
    for (size_t index = 0; index < swapchainImages_.size(); ++index) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages_[index];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainFormat_;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        checkResult(vkCreateImageView(device_, &createInfo, nullptr,
                                      &swapchainImageViews_[index]),
                    "create swapchain image view");
    }
}

void VulkanFogRenderer::createRenderPass()
{
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
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &color;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;
    checkResult(vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_),
                "create render pass");
}

void VulkanFogRenderer::createFramebuffers()
{
    framebuffers_.resize(swapchainImageViews_.size(), VK_NULL_HANDLE);
    for (size_t index = 0; index < swapchainImageViews_.size(); ++index) {
        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass_;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &swapchainImageViews_[index];
        createInfo.width = swapchainExtent_.width;
        createInfo.height = swapchainExtent_.height;
        createInfo.layers = 1;
        checkResult(vkCreateFramebuffer(device_, &createInfo, nullptr,
                                        &framebuffers_[index]),
                    "create framebuffer");
    }
}

std::vector<char> VulkanFogRenderer::readFile(const std::string& path) const
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot read shader " + path);
    }
    const std::streamsize size = input.tellg();
    input.seekg(0);
    std::vector<char> bytes(static_cast<size_t>(size));
    if (!input.read(bytes.data(), size)) {
        throw std::runtime_error("failed to read shader " + path);
    }
    return bytes;
}

VkShaderModule VulkanFogRenderer::createShaderModule(
    const std::vector<char>& code) const
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    checkResult(vkCreateShaderModule(device_, &createInfo, nullptr, &module),
                "create shader module");
    return module;
}

void VulkanFogRenderer::createShaderModules()
{
    const std::filesystem::path root = std::filesystem::path(FOG_ASSET_DIR) /
                                       "shaders";
    fogShader_ = createShaderModule(readFile((root / "fog.comp.spv").string()));
    displayVertShader_ = createShaderModule(
        readFile((root / "display.vert.spv").string()));
    displayFragShader_ = createShaderModule(
        readFile((root / "display.frag.spv").string()));
}

void VulkanFogRenderer::createDescriptorLayouts()
{
    VkDescriptorSetLayoutBinding computeBindings[4]{};
    computeBindings[0].binding = 0;
    computeBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    computeBindings[0].descriptorCount = 1;
    computeBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    for (uint32_t binding = 1; binding <= 2; ++binding) {
        computeBindings[binding].binding = binding;
        computeBindings[binding].descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        computeBindings[binding].descriptorCount = 1;
        computeBindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    computeBindings[3].binding = 3;
    computeBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    computeBindings[3].descriptorCount = 1;
    computeBindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo computeInfo{};
    computeInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    computeInfo.bindingCount = 4;
    computeInfo.pBindings = computeBindings;
    checkResult(vkCreateDescriptorSetLayout(device_, &computeInfo, nullptr,
                                            &computeSetLayout_),
                "create compute descriptor layout");

    VkDescriptorSetLayoutBinding displayBinding{};
    displayBinding.binding = 0;
    displayBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    displayBinding.descriptorCount = 1;
    displayBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo displayInfo{};
    displayInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    displayInfo.bindingCount = 1;
    displayInfo.pBindings = &displayBinding;
    checkResult(vkCreateDescriptorSetLayout(device_, &displayInfo, nullptr,
                                            &displaySetLayout_),
                "create display descriptor layout");
}

void VulkanFogRenderer::createPipelines()
{
    VkPipelineLayoutCreateInfo computeLayoutInfo{};
    computeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayoutInfo.setLayoutCount = 1;
    computeLayoutInfo.pSetLayouts = &computeSetLayout_;
    checkResult(vkCreatePipelineLayout(device_, &computeLayoutInfo, nullptr,
                                       &computePipelineLayout_),
                "create compute pipeline layout");

    VkComputePipelineCreateInfo computeInfo{};
    computeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeInfo.stage.module = fogShader_;
    computeInfo.stage.pName = "main";
    computeInfo.layout = computePipelineLayout_;
    checkResult(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1,
                                         &computeInfo, nullptr,
                                         &computePipeline_),
                "create compute pipeline");

    createDisplayPipeline();
}

void VulkanFogRenderer::createDisplayPipeline()
{
    VkPipelineLayoutCreateInfo displayLayoutInfo{};
    displayLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    displayLayoutInfo.setLayoutCount = 1;
    displayLayoutInfo.pSetLayouts = &displaySetLayout_;
    checkResult(vkCreatePipelineLayout(device_, &displayLayoutInfo, nullptr,
                                       &displayPipelineLayout_),
                "create display pipeline layout");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = displayVertShader_;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = displayFragShader_;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo graphicsInfo{};
    graphicsInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    graphicsInfo.stageCount = 2;
    graphicsInfo.pStages = stages;
    graphicsInfo.pVertexInputState = &vertexInput;
    graphicsInfo.pInputAssemblyState = &inputAssembly;
    graphicsInfo.pViewportState = &viewportState;
    graphicsInfo.pRasterizationState = &raster;
    graphicsInfo.pMultisampleState = &multisample;
    graphicsInfo.pColorBlendState = &blend;
    graphicsInfo.pDynamicState = &dynamic;
    graphicsInfo.layout = displayPipelineLayout_;
    graphicsInfo.renderPass = renderPass_;
    graphicsInfo.subpass = 0;
    checkResult(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1,
                                          &graphicsInfo, nullptr,
                                          &displayPipeline_),
                "create display pipeline");
}

uint32_t VulkanFogRenderer::findMemoryType(
    uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    for (uint32_t index = 0; index < memoryProperties_.memoryTypeCount; ++index) {
        if ((typeBits & (1u << index)) != 0 &&
            (memoryProperties_.memoryTypes[index].propertyFlags & properties) ==
                properties) {
            return index;
        }
    }
    throw std::runtime_error("no compatible Vulkan memory type");
}

void VulkanFogRenderer::createUniformAndStatsBuffers()
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(FogUniforms);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkResult(vkCreateBuffer(device_, &bufferInfo, nullptr, &uniformBuffer_),
                "create uniform buffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, uniformBuffer_, &requirements);
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    checkResult(vkAllocateMemory(device_, &allocateInfo, nullptr, &uniformMemory_),
                "allocate uniform memory");
    checkResult(vkBindBufferMemory(device_, uniformBuffer_, uniformMemory_, 0),
                "bind uniform memory");
    checkResult(vkMapMemory(device_, uniformMemory_, 0, sizeof(FogUniforms), 0,
                            &uniformMapped_),
                "map uniform memory");

    bufferInfo.size = 64;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    checkResult(vkCreateBuffer(device_, &bufferInfo, nullptr, &statsBuffer_),
                "create stats buffer");
    vkGetBufferMemoryRequirements(device_, statsBuffer_, &requirements);
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    checkResult(vkAllocateMemory(device_, &allocateInfo, nullptr, &statsMemory_),
                "allocate stats memory");
    checkResult(vkBindBufferMemory(device_, statsBuffer_, statsMemory_, 0),
                "bind stats memory");
    checkResult(vkMapMemory(device_, statsMemory_, 0, 64, 0, &statsMapped_),
                "map stats memory");
    std::memset(statsMapped_, 0, 64);
}

void VulkanFogRenderer::createDescriptorPool()
{
    VkDescriptorPoolSize sizes[4]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = 2;
    sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    sizes[1].descriptorCount = 4;
    sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sizes[2].descriptorCount = 2;
    sizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[3].descriptorCount = 2;
    VkDescriptorPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    createInfo.maxSets = 4;
    createInfo.poolSizeCount = 4;
    createInfo.pPoolSizes = sizes;
    checkResult(vkCreateDescriptorPool(device_, &createInfo, nullptr,
                                       &descriptorPool_),
                "create descriptor pool");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    checkResult(vkCreateSampler(device_, &samplerInfo, nullptr, &fogSampler_),
                "create fog sampler");
}

void VulkanFogRenderer::checkResult(VkResult result,
                                    const char* operation) const
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed: " +
                                 vkResultName(result));
    }
}

VkCommandBuffer VulkanFogRenderer::beginSingleTimeCommands() const
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkResult(vkBeginCommandBuffer(commandBuffer_, &beginInfo),
                "begin one-time command buffer");
    return commandBuffer_;
}

void VulkanFogRenderer::endSingleTimeCommands(
    VkCommandBuffer commandBuffer) const
{
    checkResult(vkEndCommandBuffer(commandBuffer), "end one-time command buffer");
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    checkResult(vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE),
                "submit one-time command buffer");
    checkResult(vkQueueWaitIdle(queue_), "wait for one-time command buffer");
}

void VulkanFogRenderer::transitionImage(
    VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout,
    VkImageLayout newLayout, VkAccessFlags sourceAccess,
    VkAccessFlags destinationAccess, VkPipelineStageFlags sourceStage,
    VkPipelineStageFlags destinationStage) const
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
}

void VulkanFogRenderer::createVolumeImages(
    const MemoryPlan& plan, VolumeResources& resources)
{
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(
        physicalDevice_, VK_FORMAT_R16G16B16A16_SFLOAT, &formatProperties);
    if ((formatProperties.optimalTilingFeatures &
         (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) !=
        (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
        throw std::runtime_error(
            "device cannot sample and store VK_FORMAT_R16G16B16A16_SFLOAT");
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {static_cast<uint32_t>(plan.width),
                        static_cast<uint32_t>(plan.height), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    for (int index = 0; index < 2; ++index) {
        checkResult(vkCreateImage(device_, &imageInfo, nullptr,
                                  &resources.images[index]),
                    "create fog history image");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, resources.images[index],
                                     &requirements);
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = findMemoryType(
            requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        checkResult(vkAllocateMemory(device_, &allocateInfo, nullptr,
                                     &resources.imageMemory[index]),
                    "allocate fog history image memory");
        checkResult(vkBindImageMemory(device_, resources.images[index],
                                      resources.imageMemory[index], 0),
                    "bind fog history image memory");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = resources.images[index];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        checkResult(vkCreateImageView(device_, &viewInfo, nullptr,
                                      &resources.imageViews[index]),
                    "create fog history image view");
    }
}

void VulkanFogRenderer::createVolumeBuffers(
    const MemoryPlan& plan, VolumeResources& resources)
{
    resources.readbackBytes =
        static_cast<VkDeviceSize>(plan.width) * plan.height * 4 *
        sizeof(uint16_t);
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = resources.readbackBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkResult(vkCreateBuffer(device_, &bufferInfo, nullptr,
                               &resources.readbackBuffer),
                "create fog readback buffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, resources.readbackBuffer,
                                  &requirements);
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    checkResult(vkAllocateMemory(device_, &allocateInfo, nullptr,
                                 &resources.readbackMemory),
                "allocate fog readback memory");
    checkResult(vkBindBufferMemory(device_, resources.readbackBuffer,
                                   resources.readbackMemory, 0),
                "bind fog readback memory");
    checkResult(vkMapMemory(device_, resources.readbackMemory, 0,
                            resources.readbackBytes, 0,
                            &resources.readbackMapped),
                "map fog readback memory");
}

void VulkanFogRenderer::clearVolumeImages(VolumeResources& resources)
{
    VkCommandBuffer command = beginSingleTimeCommands();
    VkClearColorValue clear{};
    for (int index = 0; index < 2; ++index) {
        transitionImage(command, resources.images[index],
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                        VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(command, resources.images[index],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear,
                             1, &range);
        transitionImage(command, resources.images[index],
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT |
                            VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    endSingleTimeCommands(command);
}

void VulkanFogRenderer::createVolumeResources()
{
    if (descriptorPool_ != VK_NULL_HANDLE) {
        checkResult(vkResetDescriptorPool(device_, descriptorPool_, 0),
                    "reset descriptor pool");
        computeSets_[0] = computeSets_[1] = VK_NULL_HANDLE;
        displaySets_[0] = displaySets_[1] = VK_NULL_HANDLE;
    }
    createVolumeImages(plan_, volume_);
    createVolumeBuffers(plan_, volume_);
    clearVolumeImages(volume_);
    createComputeDescriptors();
    createDisplayDescriptors();
    currentImageIndex_ = 0;
}

void VulkanFogRenderer::createComputeDescriptors()
{
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = 2;
    VkDescriptorSetLayout layouts[2] = {computeSetLayout_, computeSetLayout_};
    allocateInfo.pSetLayouts = layouts;
    checkResult(vkAllocateDescriptorSets(device_, &allocateInfo, computeSets_),
                "allocate compute descriptor sets");

    VkDescriptorBufferInfo uniformInfo{};
    uniformInfo.buffer = uniformBuffer_;
    uniformInfo.offset = 0;
    uniformInfo.range = sizeof(FogUniforms);
    VkDescriptorBufferInfo statsInfo{};
    statsInfo.buffer = statsBuffer_;
    statsInfo.offset = 0;
    statsInfo.range = 64;

    for (int outputIndex = 0; outputIndex < 2; ++outputIndex) {
        VkDescriptorImageInfo outputInfo{};
        outputInfo.imageView = volume_.imageViews[outputIndex];
        outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo historyInfo{};
        historyInfo.imageView = volume_.imageViews[1 - outputIndex];
        historyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[4]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = computeSets_[outputIndex];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &uniformInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = computeSets_[outputIndex];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &outputInfo;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = computeSets_[outputIndex];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &historyInfo;
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = computeSets_[outputIndex];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &statsInfo;
        vkUpdateDescriptorSets(device_, 4, writes, 0, nullptr);
    }
}

void VulkanFogRenderer::createDisplayDescriptors()
{
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = 2;
    VkDescriptorSetLayout layouts[2] = {displaySetLayout_, displaySetLayout_};
    allocateInfo.pSetLayouts = layouts;
    checkResult(vkAllocateDescriptorSets(device_, &allocateInfo, displaySets_),
                "allocate display descriptor sets");

    for (int index = 0; index < 2; ++index) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = fogSampler_;
        imageInfo.imageView = volume_.imageViews[index];
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = displaySets_[index];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
}

void VulkanFogRenderer::destroyVolumeResources()
{
    if (device_ != VK_NULL_HANDLE) {
        if (volume_.readbackMapped != nullptr &&
            volume_.readbackMemory != VK_NULL_HANDLE) {
            vkUnmapMemory(device_, volume_.readbackMemory);
        }
        if (volume_.readbackBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, volume_.readbackBuffer, nullptr);
        }
        if (volume_.readbackMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, volume_.readbackMemory, nullptr);
        }
        for (int index = 0; index < 2; ++index) {
            if (volume_.imageViews[index] != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, volume_.imageViews[index], nullptr);
            }
            if (volume_.images[index] != VK_NULL_HANDLE) {
                vkDestroyImage(device_, volume_.images[index], nullptr);
            }
            if (volume_.imageMemory[index] != VK_NULL_HANDLE) {
                vkFreeMemory(device_, volume_.imageMemory[index], nullptr);
            }
        }
    }
    volume_ = VolumeResources{};
}

FogUniforms VulkanFogRenderer::makeUniforms(uint64_t frameIndex) const
{
    if (restoredUniformsValid_) {
        FogUniforms restored = restoredUniforms_;
        restored.frame[0] = static_cast<float>(frameIndex);
        restored.frame[1] = static_cast<float>(plan_.width);
        restored.frame[2] = static_cast<float>(plan_.height);
        restored.sampling[0] = static_cast<float>(plan_.steps);
        return restored;
    }
    FogConfig effective = config_;
    effective.width = plan_.width;
    effective.height = plan_.height;
    effective.steps = plan_.steps;
    return makeFogUniforms(effective, frameIndex);
}

void VulkanFogRenderer::renderFrame(uint64_t frameIndex, bool present)
{
    if (!initialized_) {
        throw std::runtime_error("renderer is not initialized");
    }
    if (framebufferResized_) {
        recreateSwapchain();
    }

    uint32_t swapchainImageIndex = 0;
    bool acquired = false;
    if (present) {
        VkResult acquire = vkAcquireNextImageKHR(
            device_, swapchain_, std::numeric_limits<uint64_t>::max(),
            imageAvailable_, VK_NULL_HANDLE, &swapchainImageIndex);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
            checkResult(acquire, "acquire swapchain image");
        }
        acquired = true;
    }

    const FogUniforms uniforms = makeUniforms(frameIndex);
    std::memcpy(uniformMapped_, &uniforms, sizeof(uniforms));
    const int writeIndex = 1 - currentImageIndex_;

    checkResult(vkResetFences(device_, 1, &frameFence_), "reset frame fence");
    checkResult(vkResetCommandBuffer(commandBuffer_, 0), "reset command buffer");
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkResult(vkBeginCommandBuffer(commandBuffer_, &beginInfo),
                "begin render command buffer");

    vkCmdFillBuffer(commandBuffer_, statsBuffer_, 0, 64, 0);
    VkBufferMemoryBarrier statsBarrier{};
    statsBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    statsBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    statsBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                 VK_ACCESS_SHADER_WRITE_BIT;
    statsBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    statsBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    statsBarrier.buffer = statsBuffer_;
    statsBarrier.offset = 0;
    statsBarrier.size = 64;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                         1, &statsBarrier, 0, nullptr);

    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
                      computePipeline_);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
                            computePipelineLayout_, 0, 1,
                            &computeSets_[writeIndex], 0, nullptr);
    const uint32_t groupsX = (static_cast<uint32_t>(plan_.width) + 7u) / 8u;
    const uint32_t groupsY = (static_cast<uint32_t>(plan_.height) + 7u) / 8u;
    vkCmdDispatch(commandBuffer_, groupsX, groupsY, 1);

    transitionImage(commandBuffer_, volume_.images[writeIndex],
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    if (present) {
        VkClearValue clear{};
        clear.color = {{0.015f, 0.021f, 0.032f, 1.0f}};
        VkRenderPassBeginInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderInfo.renderPass = renderPass_;
        renderInfo.framebuffer = framebuffers_[swapchainImageIndex];
        renderInfo.renderArea.offset = {0, 0};
        renderInfo.renderArea.extent = swapchainExtent_;
        renderInfo.clearValueCount = 1;
        renderInfo.pClearValues = &clear;
        vkCmdBeginRenderPass(commandBuffer_, &renderInfo,
                             VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapchainExtent_.width);
        viewport.height = static_cast<float>(swapchainExtent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{{0, 0}, swapchainExtent_};
        vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
        vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          displayPipeline_);
        vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                displayPipelineLayout_, 0, 1,
                                &displaySets_[writeIndex], 0, nullptr);
        vkCmdDraw(commandBuffer_, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer_);
    }

    checkResult(vkEndCommandBuffer(commandBuffer_), "end render command buffer");
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    if (acquired) {
        const VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable_;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished_;
    }
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer_;
    checkResult(vkQueueSubmit(queue_, 1, &submitInfo, frameFence_),
                "submit fog frame");
    checkResult(vkWaitForFences(device_, 1, &frameFence_, VK_TRUE,
                                std::numeric_limits<uint64_t>::max()),
                "wait for fog frame");

    if (acquired) {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished_;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain_;
        presentInfo.pImageIndices = &swapchainImageIndex;
        const VkResult result = vkQueuePresentKHR(queue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            framebufferResized_ = true;
        } else if (result != VK_SUCCESS) {
            checkResult(result, "present fog frame");
        }
    }

    std::memcpy(lastGpuStats_.data(), statsMapped_, sizeof(lastGpuStats_));
    currentImageIndex_ = writeIndex;
    lastFrameIndex_ = frameIndex;
}

void VulkanFogRenderer::copyImageToReadback()
{
    VkCommandBuffer command = beginSingleTimeCommands();
    transitionImage(command, volume_.images[currentImageIndex_],
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {0, 0, 0};
    copy.imageExtent = {static_cast<uint32_t>(plan_.width),
                        static_cast<uint32_t>(plan_.height), 1};
    vkCmdCopyImageToBuffer(command, volume_.images[currentImageIndex_],
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           volume_.readbackBuffer, 1, &copy);
    transitionImage(command, volume_.images[currentImageIndex_],
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    endSingleTimeCommands(command);
}

FrameSnapshot VulkanFogRenderer::readbackSnapshot()
{
    copyImageToReadback();
    const size_t pixelCount =
        static_cast<size_t>(plan_.width) * plan_.height;
    std::vector<uint16_t> halfPixels(pixelCount * 4);
    std::memcpy(halfPixels.data(), volume_.readbackMapped,
                halfPixels.size() * sizeof(uint16_t));
    FrameSnapshot snapshot;
    snapshot.image = halfPixelsToImage(plan_.width, plan_.height, halfPixels);
    snapshot.metrics = measureImage(snapshot.image);
    snapshot.gpuStats = lastGpuStats_;
    snapshot.frameIndex = lastFrameIndex_;
    return snapshot;
}

CheckpointData VulkanFogRenderer::saveCheckpoint(const std::string& path,
                                                 uint64_t nextFrame)
{
    const FrameSnapshot snapshot = readbackSnapshot();
    std::vector<uint16_t> halfPixels(snapshot.image.rgba.size());
    for (size_t index = 0; index < snapshot.image.rgba.size(); ++index) {
        halfPixels[index] = floatToHalf(snapshot.image.rgba[index]);
    }
    return writeCheckpoint(path, nextFrame, plan_, makeUniforms(nextFrame),
                           halfPixels);
}

void VulkanFogRenderer::uploadCheckpointImage(
    const CheckpointData& checkpoint)
{
    const VkDeviceSize imageBytes =
        static_cast<VkDeviceSize>(checkpoint.halfPixels.size()) *
        sizeof(uint16_t);
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    checkResult(vkCreateBuffer(device_, &bufferInfo, nullptr, &staging),
                "create checkpoint staging buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, staging, &requirements);
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    checkResult(vkAllocateMemory(device_, &allocateInfo, nullptr, &stagingMemory),
                "allocate checkpoint staging memory");
    checkResult(vkBindBufferMemory(device_, staging, stagingMemory, 0),
                "bind checkpoint staging memory");
    void* mapped = nullptr;
    checkResult(vkMapMemory(device_, stagingMemory, 0, imageBytes, 0, &mapped),
                "map checkpoint staging memory");
    std::memcpy(mapped, checkpoint.halfPixels.data(), imageBytes);
    vkUnmapMemory(device_, stagingMemory);

    VkCommandBuffer command = beginSingleTimeCommands();
    const VkAccessFlags generalAccess =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    const VkPipelineStageFlags generalStages =
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    for (int index = 0; index < 2; ++index) {
        transitionImage(command, volume_.images[index],
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        generalAccess, VK_ACCESS_TRANSFER_WRITE_BIT,
                        generalStages, VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
    VkClearColorValue clear{};
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearColorImage(command, volume_.images[1],
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1,
                         &range);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {static_cast<uint32_t>(checkpoint.width),
                        static_cast<uint32_t>(checkpoint.height), 1};
    vkCmdCopyBufferToImage(command, staging, volume_.images[0],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    for (int index = 0; index < 2; ++index) {
        transitionImage(command, volume_.images[index],
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, generalAccess,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, generalStages);
    }
    endSingleTimeCommands(command);
    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, stagingMemory, nullptr);
}

void VulkanFogRenderer::restoreCheckpoint(
    const CheckpointData& checkpoint)
{
    if (checkpoint.width <= 0 || checkpoint.height <= 0 ||
        checkpoint.halfPixels.size() !=
            static_cast<size_t>(checkpoint.width) * checkpoint.height * 4) {
        throw std::runtime_error("checkpoint image is invalid");
    }
    if (static_cast<int>(checkpoint.uniforms.frame[1] + 0.5f) !=
            checkpoint.width ||
        static_cast<int>(checkpoint.uniforms.frame[2] + 0.5f) !=
            checkpoint.height ||
        checkpoint.uniforms.sampling[0] <= 0.0f) {
        throw std::runtime_error("checkpoint uniforms do not match its header");
    }
    const bool resizeResources =
        checkpoint.width != plan_.width || checkpoint.height != plan_.height;
    plan_.width = checkpoint.width;
    plan_.height = checkpoint.height;
    plan_.tier = checkpoint.tier;
    plan_.steps = checkpoint.steps;
    plan_.estimatedBytes = memoryPlanForTier(config_, plan_.tier).estimatedBytes;
    if (resizeResources) {
        createVolumeResources();
    }
    uploadCheckpointImage(checkpoint);
    currentImageIndex_ = 0;
    lastFrameIndex_ = checkpoint.nextFrame > 0
        ? checkpoint.nextFrame - 1
        : 0;
    std::memset(statsMapped_, 0, 64);
    lastGpuStats_.fill(0);
    restoredUniforms_ = checkpoint.uniforms;
    restoredUniformsValid_ = true;
}

void VulkanFogRenderer::destroySwapchainDependent()
{
    if (device_ == VK_NULL_HANDLE || instance_ == VK_NULL_HANDLE) {
        return;
    }
    for (VkFramebuffer framebuffer : framebuffers_) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
    }
    framebuffers_.clear();
    for (VkImageView view : swapchainImageViews_) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, view, nullptr);
        }
    }
    swapchainImageViews_.clear();
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void VulkanFogRenderer::recreateSwapchain()
{
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    vkDeviceWaitIdle(device_);
    if (displayPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, displayPipeline_, nullptr);
        displayPipeline_ = VK_NULL_HANDLE;
    }
    if (displayPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, displayPipelineLayout_, nullptr);
        displayPipelineLayout_ = VK_NULL_HANDLE;
    }
    destroySwapchainDependent();
    createSwapchain();
    createSwapchainImageViews();
    createRenderPass();
    createFramebuffers();
    createDisplayPipeline();
    framebufferResized_ = false;
}

void VulkanFogRenderer::pollEvents()
{
    glfwPollEvents();
}

bool VulkanFogRenderer::shouldClose() const
{
    return window_ == nullptr || glfwWindowShouldClose(window_);
}

void VulkanFogRenderer::requestClose()
{
    if (window_ != nullptr) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}
