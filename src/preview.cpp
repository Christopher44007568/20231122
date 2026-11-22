#include "preview.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "util.h"

namespace {

void checkVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(static_cast<int>(result)));
    }
}

std::string shaderFile(const char* compiledPath) {
    std::string path = compiledPath;
    if (!std::filesystem::exists(path)) {
        std::string file = std::filesystem::path(path).filename().string();
        path = joinPath(executableDirectory(), "shaders/" + file);
    }
    return path;
}

class VulkanPreview {
public:
    explicit VulkanPreview(Image image)
        : width_(image.width), height_(image.height), sourceImage_(std::move(image)) {
        try {
            initialize();
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~VulkanPreview() {
        cleanup();
    }

    void run();

private:
    int width_ = 0;
    int height_ = 0;
    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    std::vector<VkFramebuffer> framebuffers_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    VkBuffer sourceBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory sourceMemory_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence frameFence_ = VK_NULL_HANDLE;
    Image sourceImage_;

    void initialize();
    void createSwapchain();
    void createRenderPass();
    void createPipeline();
    void createFramebuffers();
    void createSourceBuffer();
    void createCommandBuffers();
    void recordCommands();
    void drawFrame();
    void cleanup();
};

void VulkanPreview::initialize() {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window_ = glfwCreateWindow(width_, height_, "bokeh-pipeline preview", nullptr, nullptr);
    if (window_ == nullptr) throw std::runtime_error("GLFW window creation failed");

    uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (extensions == nullptr || extensionCount == 0) {
        throw std::runtime_error("GLFW did not provide Vulkan surface extensions");
    }
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "bokeh-pipeline-preview";
    application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application.pEngineName = "vulkan-swapchain-preview";
    application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceCreate{};
    instanceCreate.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreate.pApplicationInfo = &application;
    instanceCreate.enabledExtensionCount = extensionCount;
    instanceCreate.ppEnabledExtensionNames = extensions;
    checkVk(vkCreateInstance(&instanceCreate, nullptr, &instance_), "vkCreateInstance(preview)");
    checkVk(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
            "glfwCreateWindowSurface");

    uint32_t physicalCount = 0;
    checkVk(vkEnumeratePhysicalDevices(instance_, &physicalCount, nullptr),
            "vkEnumeratePhysicalDevices(preview)");
    if (physicalCount == 0) throw std::runtime_error("no Vulkan device for preview");
    std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
    checkVk(vkEnumeratePhysicalDevices(instance_, &physicalCount, physicalDevices.data()),
            "vkEnumeratePhysicalDevices(preview)");
    for (VkPhysicalDevice candidate : physicalDevices) {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (uint32_t family = 0; family < familyCount; ++family) {
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, surface_, &presentSupport);
            if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && presentSupport) {
                physicalDevice_ = candidate;
                queueFamily_ = family;
                break;
            }
        }
        if (physicalDevice_ != VK_NULL_HANDLE) break;
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error("no Vulkan queue supports graphics and presentation");
    }

    uint32_t deviceExtensionCount = 0;
    checkVk(vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr,
                                                 &deviceExtensionCount, nullptr),
            "vkEnumerateDeviceExtensionProperties(preview)");
    std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
    checkVk(vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr,
                                                 &deviceExtensionCount,
                                                 deviceExtensions.data()),
            "vkEnumerateDeviceExtensionProperties(preview)");
    bool swapchainSupported = false;
    for (const VkExtensionProperties& extension : deviceExtensions) {
        if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            swapchainSupported = true;
        }
    }
    if (!swapchainSupported) throw std::runtime_error("VK_KHR_swapchain is unavailable");

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueCreate{};
    queueCreate.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreate.queueFamilyIndex = queueFamily_;
    queueCreate.queueCount = 1;
    queueCreate.pQueuePriorities = &priority;
    const char* enabledExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceCreate{};
    deviceCreate.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreate.queueCreateInfoCount = 1;
    deviceCreate.pQueueCreateInfos = &queueCreate;
    deviceCreate.enabledExtensionCount = 1;
    deviceCreate.ppEnabledExtensionNames = enabledExtensions;
    checkVk(vkCreateDevice(physicalDevice_, &deviceCreate, nullptr, &device_),
            "vkCreateDevice(preview)");
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

    createSwapchain();
    createRenderPass();
    createPipeline();
    createFramebuffers();
    createSourceBuffer();
    createCommandBuffers();
    recordCommands();

    VkSemaphoreCreateInfo semaphoreCreate{};
    semaphoreCreate.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    checkVk(vkCreateSemaphore(device_, &semaphoreCreate, nullptr, &imageAvailable_),
            "vkCreateSemaphore(imageAvailable)");
    checkVk(vkCreateSemaphore(device_, &semaphoreCreate, nullptr, &renderFinished_),
            "vkCreateSemaphore(renderFinished)");
    VkFenceCreateInfo fenceCreate{};
    fenceCreate.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreate.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    checkVk(vkCreateFence(device_, &fenceCreate, nullptr, &frameFence_),
            "vkCreateFence(preview)");
}

void VulkanPreview::createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities{};
    checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    uint32_t formatCount = 0;
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount,
                                                 formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    VkSurfaceFormatKHR chosen = formats.front();
    for (const VkSurfaceFormatKHR& candidate : formats) {
        bool unorm = candidate.format == VK_FORMAT_R8G8B8A8_UNORM ||
                     candidate.format == VK_FORMAT_B8G8R8A8_UNORM;
        if (unorm && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = candidate;
            break;
        }
    }
    format_ = chosen.format;
    extent_ = capabilities.currentExtent;
    if (extent_.width == UINT32_MAX) {
        extent_.width = static_cast<uint32_t>(width_);
        extent_.height = static_cast<uint32_t>(height_);
        extent_.width = std::max(capabilities.minImageExtent.width,
                                 std::min(capabilities.maxImageExtent.width, extent_.width));
        extent_.height = std::max(capabilities.minImageExtent.height,
                                  std::min(capabilities.maxImageExtent.height, extent_.height));
    }
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((capabilities.supportedCompositeAlpha & alpha) == 0) {
        alpha = static_cast<VkCompositeAlphaFlagBitsKHR>(
            capabilities.supportedCompositeAlpha & (~capabilities.supportedCompositeAlpha + 1));
    }
    VkSwapchainCreateInfoKHR swapchainCreate{};
    swapchainCreate.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreate.surface = surface_;
    swapchainCreate.minImageCount = imageCount;
    swapchainCreate.imageFormat = chosen.format;
    swapchainCreate.imageColorSpace = chosen.colorSpace;
    swapchainCreate.imageExtent = extent_;
    swapchainCreate.imageArrayLayers = 1;
    swapchainCreate.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreate.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreate.preTransform = capabilities.currentTransform;
    swapchainCreate.compositeAlpha = alpha;
    swapchainCreate.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainCreate.clipped = VK_TRUE;
    checkVk(vkCreateSwapchainKHR(device_, &swapchainCreate, nullptr, &swapchain_),
            "vkCreateSwapchainKHR");
    uint32_t actualCount = 0;
    checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &actualCount, nullptr),
            "vkGetSwapchainImagesKHR(count)");
    images_.resize(actualCount);
    checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &actualCount, images_.data()),
            "vkGetSwapchainImagesKHR");
    imageViews_.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; ++i) {
        VkImageViewCreateInfo viewCreate{};
        viewCreate.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreate.image = images_[i];
        viewCreate.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCreate.format = format_;
        viewCreate.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCreate.subresourceRange.levelCount = 1;
        viewCreate.subresourceRange.layerCount = 1;
        checkVk(vkCreateImageView(device_, &viewCreate, nullptr, &imageViews_[i]),
                "vkCreateImageView(preview)");
    }
}

void VulkanPreview::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = format_;
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
    VkRenderPassCreateInfo renderPassCreate{};
    renderPassCreate.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreate.attachmentCount = 1;
    renderPassCreate.pAttachments = &colorAttachment;
    renderPassCreate.subpassCount = 1;
    renderPassCreate.pSubpasses = &subpass;
    checkVk(vkCreateRenderPass(device_, &renderPassCreate, nullptr, &renderPass_),
            "vkCreateRenderPass(preview)");
}

void VulkanPreview::createPipeline() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutCreate{};
    layoutCreate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCreate.bindingCount = 1;
    layoutCreate.pBindings = &binding;
    checkVk(vkCreateDescriptorSetLayout(device_, &layoutCreate, nullptr, &descriptorSetLayout_),
            "vkCreateDescriptorSetLayout(preview)");

    struct PreviewPushConstants {
        uint32_t width;
        uint32_t height;
    };
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(PreviewPushConstants);
    VkPipelineLayoutCreateInfo pipelineLayoutCreate{};
    pipelineLayoutCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreate.setLayoutCount = 1;
    pipelineLayoutCreate.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutCreate.pushConstantRangeCount = 1;
    pipelineLayoutCreate.pPushConstantRanges = &pushRange;
    checkVk(vkCreatePipelineLayout(device_, &pipelineLayoutCreate, nullptr, &pipelineLayout_),
            "vkCreatePipelineLayout(preview)");

    auto createModule = [&](const char* path) {
        std::vector<uint8_t> bytes = readBinaryFile(shaderFile(path));
        VkShaderModuleCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create.codeSize = bytes.size();
        create.pCode = reinterpret_cast<const uint32_t*>(bytes.data());
        VkShaderModule module = VK_NULL_HANDLE;
        checkVk(vkCreateShaderModule(device_, &create, nullptr, &module),
                "vkCreateShaderModule(preview)");
        return module;
    };
    VkShaderModule vertexModule = createModule(BOKEH_PREVIEW_VERT_PATH);
    VkShaderModule fragmentModule = createModule(BOKEH_PREVIEW_FRAG_PATH);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent_.width),
                        static_cast<float>(extent_.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent_};
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
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
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    VkGraphicsPipelineCreateInfo pipelineCreate{};
    pipelineCreate.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreate.stageCount = 2;
    pipelineCreate.pStages = stages;
    pipelineCreate.pVertexInputState = &vertexInput;
    pipelineCreate.pInputAssemblyState = &inputAssembly;
    pipelineCreate.pViewportState = &viewportState;
    pipelineCreate.pRasterizationState = &raster;
    pipelineCreate.pMultisampleState = &multisample;
    pipelineCreate.pColorBlendState = &blend;
    pipelineCreate.layout = pipelineLayout_;
    pipelineCreate.renderPass = renderPass_;
    pipelineCreate.subpass = 0;
    checkVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineCreate,
                                      nullptr, &pipeline_),
            "vkCreateGraphicsPipelines(preview)");
    vkDestroyShaderModule(device_, fragmentModule, nullptr);
    vkDestroyShaderModule(device_, vertexModule, nullptr);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolCreate{};
    poolCreate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreate.maxSets = 1;
    poolCreate.poolSizeCount = 1;
    poolCreate.pPoolSizes = &poolSize;
    checkVk(vkCreateDescriptorPool(device_, &poolCreate, nullptr, &descriptorPool_),
            "vkCreateDescriptorPool(preview)");
    VkDescriptorSetAllocateInfo setAllocate{};
    setAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocate.descriptorPool = descriptorPool_;
    setAllocate.descriptorSetCount = 1;
    setAllocate.pSetLayouts = &descriptorSetLayout_;
    checkVk(vkAllocateDescriptorSets(device_, &setAllocate, &descriptorSet_),
            "vkAllocateDescriptorSets(preview)");
}

void VulkanPreview::createFramebuffers() {
    framebuffers_.resize(imageViews_.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < imageViews_.size(); ++i) {
        VkFramebufferCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        create.renderPass = renderPass_;
        create.attachmentCount = 1;
        create.pAttachments = &imageViews_[i];
        create.width = extent_.width;
        create.height = extent_.height;
        create.layers = 1;
        checkVk(vkCreateFramebuffer(device_, &create, nullptr, &framebuffers_[i]),
                "vkCreateFramebuffer(preview)");
    }
}

void VulkanPreview::createSourceBuffer() {
    VkDeviceSize bytes = static_cast<VkDeviceSize>(sourceImage_.rgba.size());
    VkBufferCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create.size = bytes;
    create.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateBuffer(device_, &create, nullptr, &sourceBuffer_),
            "vkCreateBuffer(preview)");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, sourceBuffer_, &requirements);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
    uint32_t memoryType = std::numeric_limits<uint32_t>::max();
    VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & (1U << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & wanted) == wanted) {
            memoryType = i;
            break;
        }
    }
    if (memoryType == std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("no host-visible memory for preview buffer");
    }
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType;
    checkVk(vkAllocateMemory(device_, &allocate, nullptr, &sourceMemory_),
            "vkAllocateMemory(preview)");
    checkVk(vkBindBufferMemory(device_, sourceBuffer_, sourceMemory_, 0),
            "vkBindBufferMemory(preview)");
    void* mapped = nullptr;
    checkVk(vkMapMemory(device_, sourceMemory_, 0, VK_WHOLE_SIZE, 0, &mapped),
            "vkMapMemory(preview)");
    std::memcpy(mapped, sourceImage_.rgba.data(), sourceImage_.rgba.size());
    vkUnmapMemory(device_, sourceMemory_);
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = sourceBuffer_;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void VulkanPreview::createCommandBuffers() {
    VkCommandPoolCreateInfo poolCreate{};
    poolCreate.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCreate.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCreate.queueFamilyIndex = queueFamily_;
    checkVk(vkCreateCommandPool(device_, &poolCreate, nullptr, &commandPool_),
            "vkCreateCommandPool(preview)");
    commandBuffers_.resize(images_.size(), VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = commandPool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    checkVk(vkAllocateCommandBuffers(device_, &allocate, commandBuffers_.data()),
            "vkAllocateCommandBuffers(preview)");
}

void VulkanPreview::recordCommands() {
    for (size_t i = 0; i < commandBuffers_.size(); ++i) {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        checkVk(vkBeginCommandBuffer(commandBuffers_[i], &begin),
                "vkBeginCommandBuffer(preview)");
        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkRenderPassBeginInfo renderPassBegin{};
        renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBegin.renderPass = renderPass_;
        renderPassBegin.framebuffer = framebuffers_[i];
        renderPassBegin.renderArea.extent = extent_;
        renderPassBegin.clearValueCount = 1;
        renderPassBegin.pClearValues = &clear;
        vkCmdBeginRenderPass(commandBuffers_[i], &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffers_[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdBindDescriptorSets(commandBuffers_[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
        struct PreviewPushConstants {
            uint32_t width;
            uint32_t height;
        } constants{static_cast<uint32_t>(sourceImage_.width),
                    static_cast<uint32_t>(sourceImage_.height)};
        vkCmdPushConstants(commandBuffers_[i], pipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(constants), &constants);
        vkCmdDraw(commandBuffers_[i], 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffers_[i]);
        checkVk(vkEndCommandBuffer(commandBuffers_[i]), "vkEndCommandBuffer(preview)");
    }
}

void VulkanPreview::drawFrame() {
    checkVk(vkWaitForFences(device_, 1, &frameFence_, VK_TRUE, UINT64_MAX),
            "vkWaitForFences(preview)");
    uint32_t imageIndex = 0;
    VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                             imageAvailable_, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) return;
    checkVk(acquire, "vkAcquireNextImageKHR");
    checkVk(vkResetFences(device_, 1, &frameFence_), "vkResetFences(preview)");
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAvailable_;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffers_[imageIndex];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished_;
    checkVk(vkQueueSubmit(queue_, 1, &submit, frameFence_), "vkQueueSubmit(preview)");
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex;
    VkResult result = vkQueuePresentKHR(queue_, &present);
    if (result != VK_SUCCESS && result != VK_ERROR_OUT_OF_DATE_KHR &&
        result != VK_SUBOPTIMAL_KHR) {
        checkVk(result, "vkQueuePresentKHR");
    }
}

void VulkanPreview::run() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
            break;
        }
        drawFrame();
    }
}

void VulkanPreview::cleanup() {
    if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
    if (frameFence_ != VK_NULL_HANDLE) vkDestroyFence(device_, frameFence_, nullptr);
    if (renderFinished_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, renderFinished_, nullptr);
    }
    if (imageAvailable_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, imageAvailable_, nullptr);
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    }
    if (sourceBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, sourceBuffer_, nullptr);
    if (sourceMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, sourceMemory_, nullptr);
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    }
    for (VkFramebuffer framebuffer : framebuffers_) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
    }
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
    }
    for (VkImageView view : imageViews_) {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(device_, view, nullptr);
    }
    if (renderPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, renderPass_, nullptr);
    if (swapchain_ != VK_NULL_HANDLE) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
    if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    if (window_ != nullptr) glfwDestroyWindow(window_);
    framebuffers_.clear();
    imageViews_.clear();
    images_.clear();
    commandBuffers_.clear();
    frameFence_ = VK_NULL_HANDLE;
    renderFinished_ = VK_NULL_HANDLE;
    imageAvailable_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    sourceBuffer_ = VK_NULL_HANDLE;
    sourceMemory_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    descriptorSetLayout_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    swapchain_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    surface_ = VK_NULL_HANDLE;
    instance_ = VK_NULL_HANDLE;
    window_ = nullptr;
}

}  // namespace

bool previewImage(const std::string& ppmPath) {
    bool glfwReady = false;
    try {
        Image image = readPpm(ppmPath);
        if (!glfwInit()) {
            std::cerr << "preview: GLFW initialization failed\n";
            return false;
        }
        glfwReady = true;
        VulkanPreview preview(std::move(image));
        preview.run();
        glfwTerminate();
        return true;
    } catch (const std::exception& exception) {
        if (glfwReady) glfwTerminate();
        std::cerr << "preview: " << exception.what() << '\n';
        return false;
    }
}

