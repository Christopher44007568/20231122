#include "app/vulkan_renderer.hpp"

#include "core/hash.hpp"
#include "core/io.hpp"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace vfcr {

namespace {

void checkVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(static_cast<int>(result)));
    }
}

struct QueueFamilies {
    std::uint32_t graphics = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t present = std::numeric_limits<std::uint32_t>::max();
    bool complete() const {
        return graphics != std::numeric_limits<std::uint32_t>::max() &&
               present != std::numeric_limits<std::uint32_t>::max();
    }
};

QueueFamilies queryQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilies families;
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
    for (std::uint32_t i = 0; i < count; ++i) {
        if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U &&
            families.graphics == std::numeric_limits<std::uint32_t>::max()) {
            families.graphics = i;
        }
        VkBool32 presentSupport = VK_FALSE;
        checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport),
                "vkGetPhysicalDeviceSurfaceSupportKHR");
        if (presentSupport == VK_TRUE &&
            families.present == std::numeric_limits<std::uint32_t>::max()) {
            families.present = i;
        }
    }
    return families;
}

bool supportsSwapchain(VkPhysicalDevice device) {
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    for (const auto& extension : extensions) {
        if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<char> readBinaryFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open shader " + path);
    }
    const std::streamsize size = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if (size > 0 && !input.read(bytes.data(), size)) {
        throw std::runtime_error("cannot read shader " + path);
    }
    return bytes;
}

}  // namespace

VulkanRenderer::VulkanRenderer(GLFWwindow* window) : window_(window) {
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createDevice();
    createSwapchain();
    createImageViews();
    createRenderPass();
    loadShaderModules();
    createPipeline();
    createFramebuffers();
    createCommandPool();
    createCommandBuffer();
    createSyncObjects();
    createVertexBuffer(300000U);
}

VulkanRenderer::~VulkanRenderer() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    cleanupSwapchain();
    if (device_ != VK_NULL_HANDLE) {
        if (fence_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, fence_, nullptr);
        }
        if (imageAvailable_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, imageAvailable_, nullptr);
        }
        if (vertexBuffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, vertexBuffer_, nullptr);
        }
        if (vertexMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, vertexMemory_, nullptr);
        }
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
        }
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        }
        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
        }
        if (vertexShader_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, vertexShader_, nullptr);
        }
        if (fragmentShader_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, fragmentShader_, nullptr);
        }
        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
        }
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}

void VulkanRenderer::createInstance() {
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "VFC Replay";
    application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application.pEngineName = "VFC UI";
    application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application.apiVersion = VK_API_VERSION_1_1;

    std::uint32_t extensionCount = 0;
    const char** requiredExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (requiredExtensions == nullptr || extensionCount == 0U) {
        throw std::runtime_error("GLFW did not provide Vulkan instance extensions");
    }
    std::vector<const char*> extensions(requiredExtensions, requiredExtensions + extensionCount);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &application;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    checkVk(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
}

void VulkanRenderer::createSurface() {
    checkVk(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
            "glfwCreateWindowSurface");
}

void VulkanRenderer::pickPhysicalDevice() {
    std::uint32_t count = 0;
    checkVk(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
            "vkEnumeratePhysicalDevices");
    if (count == 0U) {
        throw std::runtime_error("no Vulkan physical device found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    checkVk(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
            "vkEnumeratePhysicalDevices");

    int bestScore = -1;
    for (VkPhysicalDevice device : devices) {
        const QueueFamilies families = queryQueueFamilies(device, surface_);
        if (!families.complete() || !supportsSwapchain(device)) {
            continue;
        }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        int score = 1;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score = 4;
        } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            score = 3;
        } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) {
            score = 2;
        }
        if (score > bestScore) {
            bestScore = score;
            physicalDevice_ = device;
            graphicsFamily_ = families.graphics;
            presentFamily_ = families.present;
        }
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error("no Vulkan device supports graphics, presentation, and swapchain");
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    std::cout << "Vulkan device: " << properties.deviceName << "\n";
}

void VulkanRenderer::createDevice() {
    const std::set<std::uint32_t> families = {graphicsFamily_, presentFamily_};
    const float priority = 1.0F;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (const std::uint32_t family : families) {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueInfos.push_back(queueInfo);
    }
    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;
    checkVk(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
            "vkCreateDevice");
    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
}

VkSurfaceFormatKHR VulkanRenderer::chooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats) const {
    if (formats.size() == 1U && formats[0].format == VK_FORMAT_UNDEFINED) {
        return {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }
    const VkFormat preferred[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB};
    for (const VkFormat format : preferred) {
        for (const VkSurfaceFormatKHR& candidate : formats) {
            if (candidate.format == format &&
                candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return candidate;
            }
        }
    }
    return formats.front();
}

VkPresentModeKHR VulkanRenderer::choosePresentMode(
    const std::vector<VkPresentModeKHR>& modes) const {
    for (const VkPresentModeKHR mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
    if (capabilities.currentExtent.width !=
        std::numeric_limits<std::uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    VkExtent2D extent{static_cast<std::uint32_t>(std::max(1, width)),
                      static_cast<std::uint32_t>(std::max(1, height))};
    extent.width = std::max(capabilities.minImageExtent.width,
                            std::min(capabilities.maxImageExtent.width, extent.width));
    extent.height = std::max(capabilities.minImageExtent.height,
                             std::min(capabilities.maxImageExtent.height, extent.height));
    return extent;
}

void VulkanRenderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities{};
    checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    std::uint32_t formatCount = 0;
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount,
                                                  formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    std::uint32_t modeCount = 0;
    checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount,
                                                       nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR");
    std::vector<VkPresentModeKHR> modes(modeCount);
    checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount,
                                                       modes.data()),
            "vkGetPhysicalDeviceSurfacePresentModesKHR");
    if (formats.empty() || modes.empty()) {
        throw std::runtime_error("surface has no formats or present modes");
    }
    const VkSurfaceFormatKHR format = chooseSurfaceFormat(formats);
    const VkPresentModeKHR presentMode = choosePresentMode(modes);
    extent_ = chooseExtent(capabilities);

    std::uint32_t imageCount = capabilities.minImageCount + 1U;
    if (capabilities.maxImageCount > 0U && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    const VkCompositeAlphaFlagBitsKHR alphas[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (const VkCompositeAlphaFlagBitsKHR candidate : alphas) {
        if ((capabilities.supportedCompositeAlpha & candidate) != 0U) {
            alpha = candidate;
            break;
        }
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = format.format;
    createInfo.imageColorSpace = format.colorSpace;
    createInfo.imageExtent = extent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const std::uint32_t families[] = {graphicsFamily_, presentFamily_};
    if (graphicsFamily_ != presentFamily_) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = families;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = alpha;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    checkVk(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_),
            "vkCreateSwapchainKHR");
    swapchainFormat_ = format.format;
    checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr),
            "vkGetSwapchainImagesKHR");
    swapchainImages_.resize(imageCount);
    checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data()),
            "vkGetSwapchainImagesKHR");
    renderFinished_.resize(swapchainImages_.size());
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkSemaphore& semaphore : renderFinished_) {
        checkVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore),
                "vkCreateSemaphore");
    }
}

void VulkanRenderer::createImageViews() {
    swapchainViews_.resize(swapchainImages_.size());
    for (std::size_t i = 0; i < swapchainImages_.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages_[i];
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
        checkVk(vkCreateImageView(device_, &createInfo, nullptr, &swapchainViews_[i]),
                "vkCreateImageView");
    }
}

void VulkanRenderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency exitDependency{};
    exitDependency.srcSubpass = 0;
    exitDependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    exitDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    exitDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    exitDependency.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    exitDependency.dstAccessMask = 0;

    const VkSubpassDependency dependencies[] = {dependency, exitDependency};
    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &colorAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 2;
    createInfo.pDependencies = dependencies;
    checkVk(vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_),
            "vkCreateRenderPass");
}

void VulkanRenderer::loadShaderModules() {
    vertexShader_ = createShaderModule(std::string(VFCR_ASSET_DIR) + "/ui.vert.spv");
    fragmentShader_ = createShaderModule(std::string(VFCR_ASSET_DIR) + "/ui.frag.spv");
}

VkShaderModule VulkanRenderer::createShaderModule(const std::string& path) const {
    const std::vector<char> code = readBinaryFile(path);
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    checkVk(vkCreateShaderModule(device_, &createInfo, nullptr, &module),
            "vkCreateShaderModule");
    return module;
}

void VulkanRenderer::createPipeline() {
    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = vertexShader_;
    vertexStage.pName = "main";
    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = fragmentShader_;
    fragmentStage.pName = "main";
    const VkPipelineShaderStageCreateInfo stages[] = {vertexStage, fragmentStage};

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(UiVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attributes[2]{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = offsetof(UiVertex, x);
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[1].offset = offsetof(UiVertex, r);
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.depthClampEnable = VK_FALSE;
    rasterization.rasterizerDiscardEnable = VK_FALSE;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0F;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                            VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 2U;
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    checkVk(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_),
            "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    checkVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                      &pipeline_),
            "vkCreateGraphicsPipelines");
}

void VulkanRenderer::createFramebuffers() {
    framebuffers_.resize(swapchainViews_.size());
    for (std::size_t i = 0; i < swapchainViews_.size(); ++i) {
        const VkImageView attachments[] = {swapchainViews_[i]};
        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass_;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = attachments;
        createInfo.width = extent_.width;
        createInfo.height = extent_.height;
        createInfo.layers = 1;
        checkVk(vkCreateFramebuffer(device_, &createInfo, nullptr, &framebuffers_[i]),
                "vkCreateFramebuffer");
    }
}

void VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.queueFamilyIndex = graphicsFamily_;
    checkVk(vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_),
            "vkCreateCommandPool");
}

void VulkanRenderer::createCommandBuffer() {
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    checkVk(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer_),
            "vkAllocateCommandBuffers");
}

void VulkanRenderer::createSyncObjects() {
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    checkVk(vkCreateFence(device_, &fenceInfo, nullptr, &fence_), "vkCreateFence");
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    checkVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailable_),
            "vkCreateSemaphore");
}

std::uint32_t VulkanRenderer::findMemoryType(std::uint32_t typeFilter,
                                             VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
    for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1U << i)) != 0U &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("no suitable Vulkan memory type");
}

void VulkanRenderer::createVertexBuffer(std::size_t capacity) {
    if (vertexMapped_ != nullptr && vertexMemory_ != VK_NULL_HANDLE) {
        vkUnmapMemory(device_, vertexMemory_);
        vertexMapped_ = nullptr;
    }
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, vertexBuffer_, nullptr);
        vertexBuffer_ = VK_NULL_HANDLE;
    }
    if (vertexMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, vertexMemory_, nullptr);
        vertexMemory_ = VK_NULL_HANDLE;
    }
    vertexCapacity_ = capacity;
    const VkDeviceSize size = static_cast<VkDeviceSize>(capacity * sizeof(UiVertex));
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateBuffer(device_, &bufferInfo, nullptr, &vertexBuffer_),
            "vkCreateBuffer(vertex)");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, vertexBuffer_, &requirements);
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    checkVk(vkAllocateMemory(device_, &allocateInfo, nullptr, &vertexMemory_),
            "vkAllocateMemory(vertex)");
    checkVk(vkBindBufferMemory(device_, vertexBuffer_, vertexMemory_, 0),
            "vkBindBufferMemory(vertex)");
    checkVk(vkMapMemory(device_, vertexMemory_, 0, VK_WHOLE_SIZE, 0, &vertexMapped_),
            "vkMapMemory(vertex)");
}

void VulkanRenderer::ensureVertexCapacity(std::size_t count) {
    if (count <= vertexCapacity_) {
        return;
    }
    vkDeviceWaitIdle(device_);
    std::size_t capacity = std::max<std::size_t>(vertexCapacity_, 300000U);
    while (capacity < count) {
        capacity *= 2U;
    }
    createVertexBuffer(capacity);
}

void VulkanRenderer::transitionImage(VkCommandBuffer commandBuffer, VkImage image,
                                     VkImageLayout oldLayout, VkImageLayout newLayout,
                                     VkAccessFlags sourceAccess, VkAccessFlags destinationAccess,
                                     VkPipelineStageFlags sourceStage,
                                     VkPipelineStageFlags destinationStage) {
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
    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);
}

void VulkanRenderer::cleanupSwapchain() {
    for (VkFramebuffer framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();
    for (VkImageView view : swapchainViews_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swapchainViews_.clear();
    for (VkSemaphore semaphore : renderFinished_) {
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    renderFinished_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    swapchainImages_.clear();
}

void VulkanRenderer::recreateSwapchain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (width == 0 || height == 0) {
        return;
    }
    vkDeviceWaitIdle(device_);
    cleanupSwapchain();
    createSwapchain();
    createImageViews();
    createFramebuffers();
}

bool VulkanRenderer::framebufferResized(int* width, int* height) const {
    glfwGetFramebufferSize(window_, width, height);
    return width != nullptr && height != nullptr &&
           (*width != static_cast<int>(extent_.width) ||
            *height != static_cast<int>(extent_.height));
}

CaptureResult VulkanRenderer::draw(const std::vector<UiVertex>& vertices,
                                   const std::string& capturePath) {
    CaptureResult capture;
    capture.path = capturePath;
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (width <= 0 || height <= 0) {
        return capture;
    }
    if (width != static_cast<int>(extent_.width) ||
        height != static_cast<int>(extent_.height)) {
        recreateSwapchain();
    }
    if (swapchain_ == VK_NULL_HANDLE) {
        capture.error = "swapchain unavailable";
        return capture;
    }
    checkVk(vkWaitForFences(device_, 1, &fence_, VK_TRUE, std::numeric_limits<std::uint64_t>::max()),
            "vkWaitForFences");
    ensureVertexCapacity(vertices.size());
    if (!vertices.empty()) {
        std::memcpy(vertexMapped_, vertices.data(), vertices.size() * sizeof(UiVertex));
    }

    std::uint32_t imageIndex = 0;
    const VkResult acquire =
        vkAcquireNextImageKHR(device_, swapchain_, std::numeric_limits<std::uint64_t>::max(),
                              imageAvailable_, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        capture.error = "swapchain recreated";
        return capture;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed with VkResult " +
                                 std::to_string(static_cast<int>(acquire)));
    }
    currentImageIndex_ = imageIndex;

    VkBuffer captureBuffer = VK_NULL_HANDLE;
    VkDeviceMemory captureMemory = VK_NULL_HANDLE;
    void* captureMapped = nullptr;
    const bool wantsCapture = !capturePath.empty();
    const VkDeviceSize imageBytes =
        static_cast<VkDeviceSize>(extent_.width) * extent_.height * 4U;
    if (wantsCapture) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = imageBytes;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        checkVk(vkCreateBuffer(device_, &bufferInfo, nullptr, &captureBuffer),
                "vkCreateBuffer(capture)");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, captureBuffer, &requirements);
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = findMemoryType(
            requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        checkVk(vkAllocateMemory(device_, &allocateInfo, nullptr, &captureMemory),
                "vkAllocateMemory(capture)");
        checkVk(vkBindBufferMemory(device_, captureBuffer, captureMemory, 0),
                "vkBindBufferMemory(capture)");
    }

    checkVk(vkResetFences(device_, 1, &fence_), "vkResetFences");
    checkVk(vkResetCommandBuffer(commandBuffer_, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    checkVk(vkBeginCommandBuffer(commandBuffer_, &beginInfo), "vkBeginCommandBuffer");
    const VkClearValue clearValue{};
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffers_[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent_;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(commandBuffer_, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkViewport viewport{0.0F, 0.0F, static_cast<float>(extent_.width),
                        static_cast<float>(extent_.height), 0.0F, 1.0F};
    VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
    const VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &vertexBuffer_, &vertexOffset);
    const float pushConstants[2] = {static_cast<float>(extent_.width),
                                    static_cast<float>(extent_.height)};
    vkCmdPushConstants(commandBuffer_, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(pushConstants), pushConstants);
    vkCmdDraw(commandBuffer_, static_cast<std::uint32_t>(vertices.size()), 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer_);

    if (wantsCapture) {
        transitionImage(commandBuffer_, swapchainImages_[imageIndex],
                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0,
                        VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {extent_.width, extent_.height, 1};
        vkCmdCopyImageToBuffer(commandBuffer_, swapchainImages_[imageIndex],
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, captureBuffer, 1, &region);
        transitionImage(commandBuffer_, swapchainImages_[imageIndex],
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT, 0,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
    checkVk(vkEndCommandBuffer(commandBuffer_), "vkEndCommandBuffer");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailable_;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer_;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished_[imageIndex];
    checkVk(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, fence_), "vkQueueSubmit");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished_[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
    checkVk(vkWaitForFences(device_, 1, &fence_, VK_TRUE,
                            std::numeric_limits<std::uint64_t>::max()),
            "vkWaitForFences(capture)");

    if (wantsCapture) {
        checkVk(vkMapMemory(device_, captureMemory, 0, imageBytes, 0, &captureMapped),
                "vkMapMemory(capture)");
        const auto* source = static_cast<const std::uint8_t*>(captureMapped);
        std::string ppm;
        std::ostringstream header;
        header << "P6\n" << extent_.width << " " << extent_.height << "\n255\n";
        ppm = header.str();
        ppm.resize(header.str().size() +
                   static_cast<std::size_t>(extent_.width) * extent_.height * 3U);
        std::size_t output = header.str().size();
        const bool bgra = swapchainFormat_ == VK_FORMAT_B8G8R8A8_UNORM ||
                          swapchainFormat_ == VK_FORMAT_B8G8R8A8_SRGB;
        for (std::size_t i = 0;
             i < static_cast<std::size_t>(extent_.width) * extent_.height; ++i) {
            const std::uint8_t* pixel = source + i * 4U;
            if (bgra) {
                ppm[output++] = static_cast<char>(pixel[2]);
                ppm[output++] = static_cast<char>(pixel[1]);
                ppm[output++] = static_cast<char>(pixel[0]);
            } else {
                ppm[output++] = static_cast<char>(pixel[0]);
                ppm[output++] = static_cast<char>(pixel[1]);
                ppm[output++] = static_cast<char>(pixel[2]);
            }
        }
        vkUnmapMemory(device_, captureMemory);
        std::string writeError;
        if (!writeBinaryFileAtomic(capturePath, ppm.data(), ppm.size(), &writeError)) {
            capture.error = writeError;
        } else {
            capture.saved = true;
            capture.sha256 = sha256(ppm);
            capture.width = static_cast<int>(extent_.width);
            capture.height = static_cast<int>(extent_.height);
        }
        vkDestroyBuffer(device_, captureBuffer, nullptr);
        vkFreeMemory(device_, captureMemory, nullptr);
    }

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed with VkResult " +
                                 std::to_string(static_cast<int>(presentResult)));
    }
    return capture;
}

}  // namespace vfcr


