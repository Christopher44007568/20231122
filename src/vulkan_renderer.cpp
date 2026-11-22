#include "vulkan_renderer.hpp"

#include "sha256.hpp"
#include "shaders_spv.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace offscreen {
namespace {

void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed with VkResult " +
                                 std::to_string(static_cast<int>(result)));
    }
}

std::string versionString(std::uint32_t version) {
    std::ostringstream out;
    out << VK_VERSION_MAJOR(version) << '.' << VK_VERSION_MINOR(version) << '.'
        << VK_VERSION_PATCH(version);
    return out.str();
}

std::string glfwErrorText;

void glfwErrorCallback(int code, const char* description) {
    glfwErrorText = "GLFW error " + std::to_string(code) + ": " +
                    (description ? description : "unknown");
}

std::string platformName(int platform) {
    switch (platform) {
#ifdef GLFW_PLATFORM_WIN32
        case GLFW_PLATFORM_WIN32: return "win32";
        case GLFW_PLATFORM_COCOA: return "cocoa";
        case GLFW_PLATFORM_WAYLAND: return "wayland";
        case GLFW_PLATFORM_X11: return "x11";
        case GLFW_PLATFORM_NULL: return "null";
#endif
        default: return "unknown";
    }
}

std::uint32_t findMemoryType(VkPhysicalDevice physical, std::uint32_t typeBits,
                             VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    throw std::runtime_error("no compatible Vulkan memory type");
}

struct PushConstants {
    float bg[4];
    float accent[4];
    float line[4];
    std::uint32_t digest[4];
    std::uint32_t badge;
    std::uint32_t stripes;
    std::uint32_t submitCount;
    std::uint32_t titleSerial;
};

static_assert(sizeof(PushConstants) == 80, "push constant layout must match GLSL");

std::array<float, 4> colorToFloat(std::uint32_t color) {
    return {(static_cast<float>((color >> 16) & 0xffu) / 255.0f),
            (static_cast<float>((color >> 8) & 0xffu) / 255.0f),
            (static_cast<float>(color & 0xffu) / 255.0f), 1.0f};
}

}  // namespace

VulkanRenderer::VulkanRenderer(const RenderConfig& config) : config_(config) {
    if (config_.width == 0 || config_.height == 0) {
        throw std::runtime_error("render dimensions must be non-zero");
    }
    glfwSetErrorCallback(glfwErrorCallback);
#if defined(GLFW_PLATFORM_NULL)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
#endif
    if (!glfwInit()) {
        throw std::runtime_error("GLFW init failed: " + glfwErrorText);
    }
    glfwReady_ = true;
    glfwPlatform_ = platformName(glfwGetPlatform());
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window_ = glfwCreateWindow(1, 1, "offscreen-replay", nullptr, nullptr);
    glfwErrorText.clear();

    createInstance();
    pickDevice();
    createDevice();
    createTarget();
    createPipeline();
    createCommands();
}

VulkanRenderer::~VulkanRenderer() { destroy(); }

void VulkanRenderer::createInstance() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "offscreen-replay";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "none";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_0;

    std::vector<const char*> extensions;
    if (window_ != nullptr) {
        std::uint32_t count = 0;
        const char** required = glfwGetRequiredInstanceExtensions(&count);
        for (std::uint32_t i = 0; i < count; ++i) extensions.push_back(required[i]);
    }

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
    check(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance");
}

void VulkanRenderer::pickDevice() {
    std::uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
          "vkEnumeratePhysicalDevices(count)");
    if (count == 0) throw std::runtime_error("no Vulkan physical devices");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
          "vkEnumeratePhysicalDevices");

    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        const VkPhysicalDeviceType type = properties.deviceType;
        const int score = type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
                          : type == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                          : type == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2
                          : type == VK_PHYSICAL_DEVICE_TYPE_CPU ? 1
                          : 0;
        VkFormatProperties format{};
        vkGetPhysicalDeviceFormatProperties(candidate, VK_FORMAT_R8G8B8A8_UNORM,
                                            &format);
        const VkFormatFeatureFlags required =
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
        if ((format.optimalTilingFeatures & required) != required) {
            continue;
        }
        std::uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families,
                                                 queueFamilies.data());
        bool hasGraphics = false;
        std::uint32_t family = 0;
        for (std::uint32_t i = 0; i < families; ++i) {
            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                queueFamilies[i].queueCount > 0) {
                hasGraphics = true;
                family = i;
                break;
            }
        }
        if (!hasGraphics || score <= bestScore) continue;
        bestScore = score;
        physicalDevice_ = candidate;
        queueFamily_ = family;
        deviceName_ = properties.deviceName;
        driverName_ = versionString(properties.driverVersion);
        apiVersion_ = versionString(properties.apiVersion);
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error("no Vulkan device supports offscreen RGBA rendering");
    }
}

void VulkanRenderer::createDevice() {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    check(vkCreateDevice(physicalDevice_, &info, nullptr, &device_),
          "vkCreateDevice");
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
}

void VulkanRenderer::createTarget() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {config_.width, config_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device_, &imageInfo, nullptr, &image_), "vkCreateImage");

    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(device_, image_, &memoryRequirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = memoryRequirements.size;
    allocation.memoryTypeIndex =
        findMemoryType(physicalDevice_, memoryRequirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_, &allocation, nullptr, &imageMemory_),
          "vkAllocateMemory(image)");
    check(vkBindImageMemory(device_, image_, imageMemory_, 0),
          "vkBindImageMemory");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(device_, &viewInfo, nullptr, &imageView_),
          "vkCreateImageView");

    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    check(vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_),
          "vkCreateRenderPass");

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass_;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &imageView_;
    framebufferInfo.width = config_.width;
    framebufferInfo.height = config_.height;
    framebufferInfo.layers = 1;
    check(vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffer_),
          "vkCreateFramebuffer");
}

void VulkanRenderer::createPipeline() {
    auto makeShader = [this](const std::uint32_t* code, std::size_t wordCount,
                             VkShaderStageFlagBits stage) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = wordCount * sizeof(std::uint32_t);
        info.pCode = code;
        VkShaderModule module = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device_, &info, nullptr, &module),
              "vkCreateShaderModule");
        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = stage;
        stageInfo.module = module;
        stageInfo.pName = "main";
        return std::pair<VkShaderModule, VkPipelineShaderStageCreateInfo>(
            module, stageInfo);
    };

    auto vert = makeShader(kSceneVertSpv, kSceneVertSpvWords,
                           VK_SHADER_STAGE_VERTEX_BIT);
    auto frag = makeShader(kSceneFragSpv, kSceneFragSpvWords,
                           VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = {vert.second, frag.second};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(config_.width),
                        static_cast<float>(config_.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {config_.width, config_.height}};
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
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                     VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT |
                                     VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushConstants);
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_),
          "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                    nullptr, &pipeline_),
          "vkCreateGraphicsPipelines");
    vkDestroyShaderModule(device_, vert.first, nullptr);
    vkDestroyShaderModule(device_, frag.first, nullptr);
}

void VulkanRenderer::createCommands() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily_;
    check(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_),
          "vkCreateCommandPool");

    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = commandPool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device_, &allocate, &commandBuffer_),
          "vkAllocateCommandBuffers");

    const VkDeviceSize size =
        static_cast<VkDeviceSize>(config_.width) * config_.height * 4;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device_, &bufferInfo, nullptr, &stagingBuffer_),
          "vkCreateBuffer(staging)");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, stagingBuffer_, &requirements);
    VkMemoryAllocateInfo memoryInfo{};
    memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryInfo.allocationSize = requirements.size;
    memoryInfo.memoryTypeIndex =
        findMemoryType(physicalDevice_, requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_, &memoryInfo, nullptr, &stagingMemory_),
          "vkAllocateMemory(staging)");
    check(vkBindBufferMemory(device_, stagingBuffer_, stagingMemory_, 0),
          "vkBindBufferMemory");

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(device_, &fenceInfo, nullptr, &fence_), "vkCreateFence");
}

void VulkanRenderer::submitAndWait() {
    check(vkResetFences(device_, 1, &fence_), "vkResetFences");
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer_;
    check(vkQueueSubmit(queue_, 1, &submit, fence_), "vkQueueSubmit");
    check(vkWaitForFences(device_, 1, &fence_, VK_TRUE,
                          std::numeric_limits<std::uint64_t>::max()),
          "vkWaitForFences");
}

Capture VulkanRenderer::capture(const VisualState& state) {
    check(vkResetCommandBuffer(commandBuffer_, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer_, &begin), "vkBeginCommandBuffer");

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderPassBeginInfo renderBegin{};
    renderBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderBegin.renderPass = renderPass_;
    renderBegin.framebuffer = framebuffer_;
    renderBegin.renderArea = {{0, 0}, {config_.width, config_.height}};
    renderBegin.clearValueCount = 1;
    renderBegin.pClearValues = &clear;
    vkCmdBeginRenderPass(commandBuffer_, &renderBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    PushConstants push{};
    const auto bg = colorToFloat(state.bg);
    const auto accent = colorToFloat(state.accent);
    const std::array<float, 4> line{{0.72f, 0.80f, 0.88f, 1.0f}};
    std::copy(bg.begin(), bg.end(), push.bg);
    std::copy(accent.begin(), accent.end(), push.accent);
    std::copy(line.begin(), line.end(), push.line);
    const auto digest = state.digestWords();
    std::copy(digest.begin(), digest.end(), push.digest);
    push.badge = state.badge;
    push.stripes = state.stripes;
    push.submitCount = state.submitCount;
    push.titleSerial = state.titleSerial;
    vkCmdPushConstants(commandBuffer_, pipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(commandBuffer_, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer_);

    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageOffset = {0, 0, 0};
    copy.imageExtent = {config_.width, config_.height, 1};
    vkCmdCopyImageToBuffer(commandBuffer_, image_,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer_,
                           1, &copy);
    check(vkEndCommandBuffer(commandBuffer_), "vkEndCommandBuffer");
    submitAndWait();

    const VkDeviceSize size =
        static_cast<VkDeviceSize>(config_.width) * config_.height * 4;
    void* mapped = nullptr;
    check(vkMapMemory(device_, stagingMemory_, 0, size, 0, &mapped),
          "vkMapMemory");
    Capture result;
    result.width = config_.width;
    result.height = config_.height;
    result.rgba.resize(static_cast<std::size_t>(size));
    std::memcpy(result.rgba.data(), mapped, static_cast<std::size_t>(size));
    vkUnmapMemory(device_, stagingMemory_);
    result.sha256 = sha256Hex(result.rgba);
    return result;
}

void VulkanRenderer::destroy() {
    if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
    if (fence_ != VK_NULL_HANDLE) vkDestroyFence(device_, fence_, nullptr);
    if (stagingBuffer_ != VK_NULL_HANDLE)
        vkDestroyBuffer(device_, stagingBuffer_, nullptr);
    if (stagingMemory_ != VK_NULL_HANDLE)
        vkFreeMemory(device_, stagingMemory_, nullptr);
    if (commandPool_ != VK_NULL_HANDLE)
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    if (pipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (framebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, framebuffer_, nullptr);
    if (renderPass_ != VK_NULL_HANDLE)
        vkDestroyRenderPass(device_, renderPass_, nullptr);
    if (imageView_ != VK_NULL_HANDLE)
        vkDestroyImageView(device_, imageView_, nullptr);
    if (image_ != VK_NULL_HANDLE) vkDestroyImage(device_, image_, nullptr);
    if (imageMemory_ != VK_NULL_HANDLE)
        vkFreeMemory(device_, imageMemory_, nullptr);
    if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    if (window_ != nullptr) glfwDestroyWindow(window_);
    if (glfwReady_) glfwTerminate();
}

}  // namespace offscreen
