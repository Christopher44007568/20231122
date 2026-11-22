#include "renderer.hpp"

#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace grid {

namespace {

std::string vkResultString(VkResult result) {
    return "VkResult=" + std::to_string(static_cast<int>(result));
}

uint32_t findMemoryType(VkPhysicalDevice device, uint32_t typeFilter,
                        VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(device, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

std::vector<char> readBinaryFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::fprintf(stderr, "[validation] %s\n",
                     data && data->pMessage ? data->pMessage : "(null)");
    }
    return VK_FALSE;
}

const char* layoutName(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
        case VK_IMAGE_LAYOUT_GENERAL: return "GENERAL";
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return "COLOR_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return "TRANSFER_SRC_OPTIMAL";
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return "TRANSFER_DST_OPTIMAL";
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return "PRESENT_SRC_KHR";
        default: return "OTHER";
    }
}

}  // namespace

Renderer::~Renderer() { shutdown(); }

bool Renderer::initGlfw(const Config& config, std::string* error) {
    if (!glfwInit()) {
        *error = "glfwInit 失败（无显示环境时仍可使用 headless 模式，GLFW 仅做平台初始化）";
        return false;
    }
    glfwInitialized_ = true;
    if (config.headless) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
    evidence_->event("glfw", std::string("GLFW 版本 ") + glfwGetVersionString());
    return true;
}

bool Renderer::createInstance(const Config& config, std::string* error) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "grid-boundary-marker";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "reviewable-grid";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions;
    for (uint32_t i = 0; i < glfwExtensionCount; ++i) {
        extensions.push_back(glfwExtensions[i]);
    }

    std::vector<const char*> layers;
    if (config.validation) {
        uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> available(count);
        vkEnumerateInstanceLayerProperties(&count, available.data());
        for (const auto& layer : available) {
            if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                layers.push_back("VK_LAYER_KHRONOS_validation");
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                validationEnabled_ = true;
                break;
            }
        }
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();

    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        *error = "vkCreateInstance 失败: " + vkResultString(result);
        return false;
    }

    if (validationEnabled_) {
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
        messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messengerInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = debugCallback;
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (create) create(instance_, &messengerInfo, nullptr, &debugMessenger_);
    }
    evidence_->event("vulkan",
                     std::string("实例已创建，validation=") +
                         (validationEnabled_ ? "on" : "off"));
    return true;
}

bool Renderer::pickPhysicalDevice(const Config&, std::string* error) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        *error = "未发现任何 Vulkan 物理设备（检查驱动 / ICD 配置 / SwiftShader 等软件实现）";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    for (VkPhysicalDevice candidate : devices) {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfProps(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qfCount, qfProps.data());
        for (uint32_t i = 0; i < qfCount; ++i) {
            if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                physicalDevice_ = candidate;
                queueFamily_ = i;
                break;
            }
        }
        if (physicalDevice_) break;
    }
    if (!physicalDevice_) {
        *error = "没有支持图形队列的物理设备";
        return false;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    deviceName_ = props.deviceName;
    apiVersion_ = props.apiVersion;
    evidence_->event("vulkan", std::string("物理设备=") + deviceName_ +
                                   " apiVersion=" + std::to_string(apiVersion_));
    return true;
}

bool Renderer::createLogicalDevice(std::string* error) {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkPhysicalDeviceFeatures features{};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 0;
    deviceInfo.pEnabledFeatures = &features;

    VkResult result = vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_);
    if (result != VK_SUCCESS) {
        *error = "vkCreateDevice 失败: " + vkResultString(result);
        return false;
    }
    vkGetDeviceQueue(device_, queueFamily_, 0, &graphicsQueue_);
    evidence_->event("vulkan", "逻辑设备与图形队列已建立");
    return true;
}

bool Renderer::createOffscreenTargets(const Config& config, std::string* error) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {config.width, config.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateImage(device_, &imageInfo, nullptr, &colorImage_);
    if (result != VK_SUCCESS) {
        *error = "创建 color image 失败: " + vkResultString(result);
        return false;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, colorImage_, &req);
    uint32_t memoryType = findMemoryType(
        physicalDevice_, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == UINT32_MAX) {
        *error = "找不到 DEVICE_LOCAL 内存类型";
        return false;
    }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &colorMemory_) != VK_SUCCESS) {
        *error = "分配 color image 内存失败";
        return false;
    }
    vkBindImageMemory(device_, colorImage_, colorMemory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = colorImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &viewInfo, nullptr, &colorView_) != VK_SUCCESS) {
        *error = "创建 color image view 失败";
        return false;
    }

    // 主机可见线性 image，作为 GPU->CPU 读回落点
    VkImageCreateInfo hostInfo = imageInfo;
    hostInfo.tiling = VK_IMAGE_TILING_LINEAR;
    hostInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    hostInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    result = vkCreateImage(device_, &hostInfo, nullptr, &hostImage_);
    if (result != VK_SUCCESS) {
        *error = "创建 host image 失败: " + vkResultString(result);
        return false;
    }
    vkGetImageMemoryRequirements(device_, hostImage_, &req);
    memoryType = findMemoryType(physicalDevice_, req.memoryTypeBits,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memoryType == UINT32_MAX) {
        *error = "找不到 HOST_VISIBLE 内存类型，无法读回帧缓冲";
        return false;
    }
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &hostMemory_) != VK_SUCCESS) {
        *error = "分配 host image 内存失败";
        return false;
    }
    vkBindImageMemory(device_, hostImage_, hostMemory_, 0);
    hostImageSize_ = req.size;

    colorLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    evidence_->event("resource", "离屏 color image 与 host readback image 已创建");
    return true;
}

bool Renderer::createRenderPassAndPipeline(const Config& config, std::string* error) {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;
    if (vkCreateRenderPass(device_, &rpInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        *error = "创建 render pass 失败";
        return false;
    }

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &colorView_;
    fbInfo.width = config.width;
    fbInfo.height = config.height;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffer_) != VK_SUCCESS) {
        *error = "创建 framebuffer 失败";
        return false;
    }

    std::string fragPath = "shaders/grid.frag.spv";
    std::vector<char> fragCode = readBinaryFile(fragPath);
    std::vector<char> bgVertCode = readBinaryFile("shaders/background.vert.spv");
    std::vector<char> lineVertCode = readBinaryFile("shaders/grid.vert.spv");
    if (bgVertCode.empty() || lineVertCode.empty() || fragCode.empty()) {
        *error = "无法读取 SPIR-V（请在构建目录运行，shaders/*.spv 由构建生成）";
        return false;
    }

    auto makeModule = [&](const std::vector<char>& code) -> VkShaderModule {
        VkShaderModuleCreateInfo shaderInfo{};
        shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.codeSize = code.size();
        shaderInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        vkCreateShaderModule(device_, &shaderInfo, nullptr, &module);
        return module;
    };
    VkShaderModule bgVertModule = makeModule(bgVertCode);
    VkShaderModule lineVertModule = makeModule(lineVertCode);
    VkShaderModule fragModule;
    fragModule = makeModule(fragCode);
    if (bgVertModule == VK_NULL_HANDLE || lineVertModule == VK_NULL_HANDLE ||
        fragModule == VK_NULL_HANDLE) {
        *error = "创建 shader module 失败";
        return false;
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = 64;  // 含 std140 对齐占位，与着色器 offset 布局一致

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        *error = "创建 pipeline layout 失败";
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(config.width),
                        static_cast<float>(config.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {config.width, config.height}};
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

    auto buildPipeline = [&](VkPrimitiveTopology topology, VkPipeline* out) -> bool {
        assembly.topology = topology;
        stages[0].module =
            topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ? bgVertModule : lineVertModule;
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &assembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pColorBlendState = &blend;
        pipelineInfo.layout = pipelineLayout_;
        pipelineInfo.renderPass = renderPass_;
        pipelineInfo.subpass = 0;
        return vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                         nullptr, out) == VK_SUCCESS;
    };

    if (!buildPipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, &pipeline_) ||
        !buildPipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, &linePipeline_)) {
        *error = "创建图形管线失败";
        return false;
    }

    vkDestroyShaderModule(device_, bgVertModule, nullptr);
    vkDestroyShaderModule(device_, lineVertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
    evidence_->event("pipeline", "TRIANGLE_LIST(背景) 与 LINE_LIST(边界) 两条管线已创建");
    return true;
}

bool Renderer::createCommandObjects(std::string* error) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily_;
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        *error = "创建 command pool 失败";
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer_) != VK_SUCCESS) {
        *error = "分配 command buffer 失败";
        return false;
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device_, &fenceInfo, nullptr, &fence_) != VK_SUCCESS) {
        *error = "创建 fence 失败";
        return false;
    }
    evidence_->event("sync", "command pool / fence 已就绪（帧完成同步统一使用 fence）");
    return true;
}

void Renderer::recordBarrier(const char* imageName, VkImageLayout from,
                             VkImageLayout to, VkPipelineStageFlags src,
                             VkPipelineStageFlags dst) {
    BarrierRecord record;
    record.sequence = static_cast<uint32_t>(lastBarriers_.size());
    record.image = imageName;
    record.fromLayout = layoutName(from);
    record.toLayout = layoutName(to);
    record.srcStage = src;
    record.dstStage = dst;
    lastBarriers_.push_back(record);

    std::ostringstream detail;
    detail << "barrier#" << record.sequence << " " << imageName << " "
           << record.fromLayout << " -> " << record.toLayout;
    evidence_->event("barrier", detail.str());
}

bool Renderer::readPixels(std::vector<uint8_t>* pixels, std::string* error) {
    VkImageSubresource subresource{};
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(device_, hostImage_, &subresource, &layout);

    void* mapped = nullptr;
    VkResult result = vkMapMemory(device_, hostMemory_, 0, hostImageSize_, 0, &mapped);
    if (result != VK_SUCCESS) {
        *error = "映射 host image 内存失败: " + vkResultString(result);
        return false;
    }
    const auto* src = static_cast<const uint8_t*>(mapped);
    pixels->resize(static_cast<size_t>(config_->width) * config_->height * 4);
    for (uint32_t row = 0; row < config_->height; ++row) {
        const uint8_t* rowStart = src + layout.offset + row * layout.rowPitch;
        std::memcpy(pixels->data() + static_cast<size_t>(row) * config_->width * 4,
                    rowStart, static_cast<size_t>(config_->width) * 4);
    }
    vkUnmapMemory(device_, hostMemory_);
    return true;
}

bool Renderer::initialize(const Config& config, EvidenceStore* evidence,
                          std::string* error) {
    config_ = &config;
    evidence_ = evidence;

    if (!initGlfw(config, error)) return false;
    if (!createInstance(config, error)) return false;
    if (!pickPhysicalDevice(config, error)) return false;
    if (!createLogicalDevice(error)) return false;
    if (!createOffscreenTargets(config, error)) return false;
    if (!createRenderPassAndPipeline(config, error)) return false;
    if (!createCommandObjects(error)) return false;

    evidence_->event("init", "渲染器初始化完成，离屏帧缓冲尺寸 " +
                                 std::to_string(config.width) + "x" +
                                 std::to_string(config.height));
    return true;
}

bool Renderer::renderFrame(uint32_t frameIndex, std::vector<uint8_t>* pixels,
                           FrameEvidence* frameEvidence, std::string* error) {
    lastDrawCalls_.clear();
    lastBarriers_.clear();

    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &fence_);
    vkResetCommandBuffer(commandBuffer_, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer_, &beginInfo) != VK_SUCCESS) {
        *error = "vkBeginCommandBuffer 失败";
        return false;
    }

    // 屏障 1：UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL（每帧显式声明资源状态）
    VkImageMemoryBarrier toColor{};
    toColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColor.image = colorImage_;
    toColor.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toColor.srcAccessMask = 0;
    toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer_,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toColor);
    // 每帧命令缓冲的 oldLayout 显式为 UNDEFINED（不保留上一帧内容，靠 clear 重绘）
    recordBarrier("colorImage", VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    colorLayout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkClearValue clearValue{};
    for (int i = 0; i < 4; ++i) {
        clearValue.color.float32[i] = config_->clearColor[i];
    }
    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = renderPass_;
    rpBegin.framebuffer = framebuffer_;
    rpBegin.renderArea.extent = {config_->width, config_->height};
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearValue;
    vkCmdBeginRenderPass(commandBuffer_, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // 四个 16 字节对齐的 vec4，与着色器 offset 布局严格一致
    struct PushData {
        float viewport[4];
        float grid[4];
        float color[4];
        float clearColor[4];
    } pushData{};
    pushData.viewport[0] = static_cast<float>(config_->width);
    pushData.viewport[1] = static_cast<float>(config_->height);
    pushData.grid[0] = static_cast<float>(config_->gridCols);
    pushData.grid[1] = static_cast<float>(config_->gridRows);
    std::memcpy(pushData.color, config_->lineColor, sizeof(pushData.color));
    std::memcpy(pushData.clearColor, config_->clearColor,
                sizeof(pushData.clearColor));

    // draw#0 背景全屏三角形
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdPushConstants(commandBuffer_, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pushData), &pushData);
    vkCmdDraw(commandBuffer_, 3, 1, 0, 0);
    lastDrawCalls_.push_back(
        {0, "background_fullscreen_triangle", 3, 1, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST});
    evidence_->event("drawcall",
                     "frame=" + std::to_string(frameIndex) +
                         " draw#0 background vkCmdDraw(vertexCount=3,instanceCount=1)");

    // draw#1 全部网格边界：每单元 8 顶点（4 条 LINE_LIST 边）
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline_);
    vkCmdPushConstants(commandBuffer_, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pushData), &pushData);
    uint32_t instances = config_->gridCols * config_->gridRows;
    vkCmdDraw(commandBuffer_, 8, instances, 0, 0);
    lastDrawCalls_.push_back(
        {1, "grid_boundary_edges", 8, instances, VK_PRIMITIVE_TOPOLOGY_LINE_LIST});
    evidence_->event("drawcall",
                     "frame=" + std::to_string(frameIndex) +
                         " draw#1 grid_boundary vkCmdDraw(vertexCount=8,instanceCount=" +
                         std::to_string(instances) + ")");

    vkCmdEndRenderPass(commandBuffer_);

    // 屏障 2：COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL
    VkImageMemoryBarrier toSrc = toColor;
    toSrc.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer_,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toSrc);
    recordBarrier("colorImage", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    colorLayout_ = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    // 屏障 3：host image UNDEFINED -> TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = hostImage_;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer_,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toDst);
    recordBarrier("hostImage", VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {static_cast<int32_t>(config_->width),
                          static_cast<int32_t>(config_->height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = blit.srcOffsets[0];
    blit.dstOffsets[1] = blit.srcOffsets[1];
    vkCmdBlitImage(commandBuffer_,
                   colorImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   hostImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_NEAREST);
    evidence_->event("transfer",
                     "frame=" + std::to_string(frameIndex) + " vkCmdBlitImage -> host image");

    if (vkEndCommandBuffer(commandBuffer_) != VK_SUCCESS) {
        *error = "vkEndCommandBuffer 失败";
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer_;
    VkResult result = vkQueueSubmit(graphicsQueue_, 1, &submitInfo, fence_);
    if (result != VK_SUCCESS) {
        *error = "vkQueueSubmit 失败: " + vkResultString(result);
        return false;
    }
    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);

    if (!readPixels(pixels, error)) return false;

    frameEvidence->drawCallCount = static_cast<uint32_t>(lastDrawCalls_.size());
    frameEvidence->finalImageLayout = layoutName(colorLayout_);
    evidence_->event("frame",
                     "frame=" + std::to_string(frameIndex) +
                         " 完成 GPU 执行并读回，draw_calls=" +
                         std::to_string(lastDrawCalls_.size()) +
                         " final_layout=" + frameEvidence->finalImageLayout);
    return true;
}

void Renderer::shutdown() {
    if (device_) vkDeviceWaitIdle(device_);
    if (fence_) vkDestroyFence(device_, fence_, nullptr);
    if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (linePipeline_) vkDestroyPipeline(device_, linePipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (framebuffer_) vkDestroyFramebuffer(device_, framebuffer_, nullptr);
    if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
    if (colorView_) vkDestroyImageView(device_, colorView_, nullptr);
    if (hostMemory_) vkFreeMemory(device_, hostMemory_, nullptr);
    if (hostImage_) vkDestroyImage(device_, hostImage_, nullptr);
    if (colorMemory_) vkFreeMemory(device_, colorMemory_, nullptr);
    if (colorImage_) vkDestroyImage(device_, colorImage_, nullptr);
    if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
    if (debugMessenger_ && instance_) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance_, debugMessenger_, nullptr);
    }
    if (device_) vkDestroyDevice(device_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
    if (glfwInitialized_) glfwTerminate();

    fence_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    linePipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    framebuffer_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    colorView_ = VK_NULL_HANDLE;
    hostMemory_ = VK_NULL_HANDLE;
    hostImage_ = VK_NULL_HANDLE;
    colorMemory_ = VK_NULL_HANDLE;
    colorImage_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    debugMessenger_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    instance_ = VK_NULL_HANDLE;
    glfwInitialized_ = false;
}

}  // namespace grid
