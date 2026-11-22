#include "vulkan_review.hpp"

#include "hash.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace lshc {
namespace {

struct GpuShadowObject {
    float centerRadius[4];
    float lightStrength[4];
    float resolutionPenumbra[4];
    float versions[4];
};
static_assert(sizeof(GpuShadowObject) == 64, "UBO layout must match shader");

template <typename Handle>
uint64_t handleValue(Handle handle)
{
    if constexpr (std::is_pointer<Handle>::value) {
        return reinterpret_cast<uint64_t>(handle);
    } else {
        return static_cast<uint64_t>(handle);
    }
}

std::vector<uint8_t> readFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read SPIR-V: " + path);
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
}

const char* resultName(VkResult result)
{
    switch (result) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        default:
            return "VK_ERROR_<other>";
    }
}

void validateRecord(const ShadowRecord& record)
{
    if (!record.integrityOk) {
        throw std::runtime_error("record failed integrity validation");
    }
    if (record.objectId == 0u) {
        throw std::runtime_error("object id must be non-zero");
    }
    if (!std::isfinite(record.centerX) || !std::isfinite(record.centerY) ||
        !std::isfinite(record.radius) || record.radius <= 0.0f) {
        throw std::runtime_error("object center/radius is not finite and positive");
    }
    const float lightLength = std::sqrt(record.lightX * record.lightX +
                                        record.lightY * record.lightY +
                                        record.lightZ * record.lightZ);
    if (!std::isfinite(lightLength) || lightLength <= 0.0001f) {
        throw std::runtime_error("light direction is degenerate");
    }
    if (!std::isfinite(record.strength) || record.strength < 0.0f ||
        record.strength > 1.0f) {
        throw std::runtime_error("shadow strength is outside [0, 1]");
    }
    if (!std::isfinite(record.penumbra) || record.penumbra < 0.0f) {
        throw std::runtime_error("penumbra is negative or non-finite");
    }
}

uint32_t chooseMemoryType(const VkPhysicalDeviceMemoryProperties& properties,
                          uint32_t typeBits, VkMemoryPropertyFlags required)
{
    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        const bool supported = (typeBits & (1u << index)) != 0u;
        const bool matches =
            (properties.memoryTypes[index].propertyFlags & required) == required;
        if (supported && matches) {
            return index;
        }
    }
    throw std::runtime_error("no compatible Vulkan memory type");
}

}  // namespace

struct VulkanReviewRenderer::Transient {
    explicit Transient(VulkanReviewRenderer* renderer) : owner(renderer) {}

    VulkanReviewRenderer* owner = nullptr;
    VkBuffer uboBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uboMemory = VK_NULL_HANDLE;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkDeviceSize uboBytes = sizeof(GpuShadowObject);
    VkDeviceSize stagingBytes = 0;
    VkDeviceSize colorMemoryBytes = 0;

    ~Transient() { owner->releaseTransient(*this); }
};

VulkanReviewRenderer::VulkanReviewRenderer(
    uint32_t width, uint32_t height, std::vector<std::string> instanceExtensions)
    : m_width(width), m_height(height)
{
    if (m_width == 0u || m_height == 0u) {
        throw std::runtime_error("framebuffer extent must be non-zero");
    }
    try {
        createInstance(std::move(instanceExtensions));
        pickDevice();
        createDevice();
        createCommandResources();
        createRenderResources();
        createDescriptorResources();
        createPipeline();
        m_initialized = true;
    } catch (...) {
        shutdown();
        throw;
    }
}

VulkanReviewRenderer::~VulkanReviewRenderer()
{
    shutdown();
}

void VulkanReviewRenderer::check(VkResult result, const char* operation) const
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed: " +
                                 resultName(result));
    }
}

std::string VulkanReviewRenderer::vulkanApiVersion() const
{
    return std::to_string(VK_VERSION_MAJOR(m_apiVersion)) + "." +
           std::to_string(VK_VERSION_MINOR(m_apiVersion)) + "." +
           std::to_string(VK_VERSION_PATCH(m_apiVersion));
}

void VulkanReviewRenderer::shutdown()
{
    if (m_device != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(m_device);
        if (m_fence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, m_fence, nullptr);
            m_fence = VK_NULL_HANDLE;
        }
        if (m_commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;
            m_commandBuffer = VK_NULL_HANDLE;
        }
        if (m_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_pipeline, nullptr);
            m_pipeline = VK_NULL_HANDLE;
        }
        if (m_pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }
        if (m_descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
            m_descriptorSet = VK_NULL_HANDLE;
        }
        if (m_descriptorLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_descriptorLayout, nullptr);
            m_descriptorLayout = VK_NULL_HANDLE;
        }
        if (m_fragmentShader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, m_fragmentShader, nullptr);
            m_fragmentShader = VK_NULL_HANDLE;
        }
        if (m_vertexShader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, m_vertexShader, nullptr);
            m_vertexShader = VK_NULL_HANDLE;
        }
        if (m_renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device, m_renderPass, nullptr);
            m_renderPass = VK_NULL_HANDLE;
        }
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    m_initialized = false;
}

void VulkanReviewRenderer::releaseTransient(Transient& transient)
{
    if (m_device == VK_NULL_HANDLE) {
        return;
    }
    if (transient.framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, transient.framebuffer, nullptr);
        transient.framebuffer = VK_NULL_HANDLE;
        --m_liveTransientObjects;
    }
    if (transient.colorView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, transient.colorView, nullptr);
        transient.colorView = VK_NULL_HANDLE;
        --m_liveTransientObjects;
    }
    if (transient.colorImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, transient.colorImage, nullptr);
        transient.colorImage = VK_NULL_HANDLE;
        --m_liveTransientObjects;
    }
    if (transient.colorMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, transient.colorMemory, nullptr);
        transient.colorMemory = VK_NULL_HANDLE;
        --m_liveTransientObjects;
    }
    if (transient.stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, transient.stagingBuffer, nullptr);
        transient.stagingBuffer = VK_NULL_HANDLE;
        --m_liveTransientObjects;
    }
    if (transient.stagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, transient.stagingMemory, nullptr);
        transient.stagingMemory = VK_NULL_HANDLE;
        --m_liveTransientObjects;
    }
    if (transient.uboBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, transient.uboBuffer, nullptr);
        transient.uboBuffer = VK_NULL_HANDLE;
        --m_liveTransientObjects;
    }
    if (transient.uboMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, transient.uboMemory, nullptr);
        transient.uboMemory = VK_NULL_HANDLE;
        --m_liveTransientObjects;
    }
}

void VulkanReviewRenderer::createInstance(
    std::vector<std::string> extensions)
{
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "lshc_review";
    application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application.pEngineName = "lshc_review";
    application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application.apiVersion = VK_API_VERSION_1_1;

    std::vector<const char*> extensionNames;
    extensionNames.reserve(extensions.size());
    for (const std::string& extension : extensions) {
        extensionNames.push_back(extension.c_str());
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &application;
    createInfo.enabledExtensionCount =
        static_cast<uint32_t>(extensionNames.size());
    createInfo.ppEnabledExtensionNames =
        extensionNames.empty() ? nullptr : extensionNames.data();
    check(vkCreateInstance(&createInfo, nullptr, &m_instance),
          "vkCreateInstance");
}

void VulkanReviewRenderer::pickDevice()
{
    uint32_t deviceCount = 0;
    check(vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr),
          "vkEnumeratePhysicalDevices");
    if (deviceCount == 0u) {
        throw std::runtime_error("no Vulkan physical device is available");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    check(vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data()),
          "vkEnumeratePhysicalDevices");

    for (VkPhysicalDevice device : devices) {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount,
                                                 families.data());
        for (uint32_t family = 0; family < familyCount; ++family) {
            if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
                m_physicalDevice = device;
                m_queueFamily = family;
                break;
            }
        }
        if (m_physicalDevice != VK_NULL_HANDLE) {
            break;
        }
    }
    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("no Vulkan device exposes a graphics queue");
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    m_deviceName = properties.deviceName;
    m_driverName = "Vulkan " +
                   std::to_string(VK_VERSION_MAJOR(properties.apiVersion)) + "." +
                   std::to_string(VK_VERSION_MINOR(properties.apiVersion)) + "." +
                   std::to_string(VK_VERSION_PATCH(properties.apiVersion));
    if (properties.apiVersion >= VK_API_VERSION_1_2) {
        VkPhysicalDeviceDriverProperties driverProperties{};
        driverProperties.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        VkPhysicalDeviceProperties2 properties2{};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &driverProperties;
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &properties2);
        if (driverProperties.driverName[0] != '\0') {
            m_driverName = driverProperties.driverName;
        }
    }
    m_apiVersion = properties.apiVersion;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memoryProperties);
}

void VulkanReviewRenderer::createDevice()
{
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    check(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device),
          "vkCreateDevice");
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    if (m_queue == VK_NULL_HANDLE) {
        throw std::runtime_error("vkGetDeviceQueue returned a null queue");
    }
}

void VulkanReviewRenderer::createCommandResources()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamily;
    check(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool),
          "vkCreateCommandPool");

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = m_commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(m_device, &allocateInfo, &m_commandBuffer),
          "vkAllocateCommandBuffers");

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(m_device, &fenceInfo, nullptr, &m_fence),
          "vkCreateFence");
}

void VulkanReviewRenderer::createRenderResources()
{
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

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
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency readbackDependency{};
    readbackDependency.srcSubpass = 0;
    readbackDependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    readbackDependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    readbackDependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    readbackDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    readbackDependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    const std::array<VkSubpassDependency, 2> dependencies = {
        dependency, readbackDependency};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount =
        static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    check(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass),
          "vkCreateRenderPass");

    const auto vertexCode = readFile(std::string(SHADER_DIR) +
                                     "/shadow.vert.spv");
    const auto fragmentCode = readFile(std::string(SHADER_DIR) +
                                       "/shadow.frag.spv");
    auto createShader = [this](const std::vector<uint8_t>& code) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        check(vkCreateShaderModule(m_device, &info, nullptr, &module),
              "vkCreateShaderModule");
        return module;
    };
    m_vertexShader = createShader(vertexCode);
    m_fragmentShader = createShader(fragmentCode);
}

void VulkanReviewRenderer::createDescriptorResources()
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr,
                                      &m_descriptorLayout),
          "vkCreateDescriptorSetLayout");

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    check(vkCreateDescriptorPool(m_device, &poolInfo, nullptr,
                                 &m_descriptorPool),
          "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_descriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &m_descriptorLayout;
    check(vkAllocateDescriptorSets(m_device, &allocateInfo, &m_descriptorSet),
          "vkAllocateDescriptorSets");
}

void VulkanReviewRenderer::createPipeline()
{
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorLayout;
    check(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr,
                                 &m_pipelineLayout),
          "vkCreatePipelineLayout");

    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = m_vertexShader;
    vertexStage.pName = "main";
    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = m_fragmentShader;
    fragmentStage.pName = "main";
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {
        vertexStage, fragmentStage};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.depthClampEnable = VK_FALSE;
    rasterization.rasterizerDiscardEnable = VK_FALSE;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.depthBiasEnable = VK_FALSE;
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample.sampleShadingEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_FALSE;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                     VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT |
                                     VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.logicOpEnable = VK_FALSE;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    const std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount =
        static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;
    check(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                    nullptr, &m_pipeline),
          "vkCreateGraphicsPipelines");
}

RenderEvidence VulkanReviewRenderer::renderShadow(const ShadowRecord& record,
                                                   const std::string& side)
{
    if (!m_initialized) {
        throw std::runtime_error("Vulkan renderer is not initialized");
    }
    validateRecord(record);
    Transient transient(this);
    return renderOnce(record, side, transient);
}

RenderEvidence VulkanReviewRenderer::renderOnce(
    const ShadowRecord& record, const std::string& side, Transient& transient)
{
    (void)side;
    const VkBufferUsageFlags hostUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VkBufferCreateInfo uboInfo{};
    uboInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    uboInfo.size = transient.uboBytes;
    uboInfo.usage = hostUsage;
    uboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(m_device, &uboInfo, nullptr, &transient.uboBuffer),
          "vkCreateBuffer(ubo)");
    ++m_liveTransientObjects;

    VkMemoryRequirements uboRequirements{};
    vkGetBufferMemoryRequirements(m_device, transient.uboBuffer,
                                  &uboRequirements);
    VkMemoryAllocateInfo uboAllocate{};
    uboAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    uboAllocate.allocationSize = uboRequirements.size;
    uboAllocate.memoryTypeIndex = chooseMemoryType(
        m_memoryProperties, uboRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(m_device, &uboAllocate, nullptr,
                           &transient.uboMemory),
          "vkAllocateMemory(ubo)");
    ++m_liveTransientObjects;
    check(vkBindBufferMemory(m_device, transient.uboBuffer,
                             transient.uboMemory, 0),
          "vkBindBufferMemory(ubo)");

    GpuShadowObject object{};
    object.centerRadius[0] = record.centerX;
    object.centerRadius[1] = record.centerY;
    object.centerRadius[2] = record.radius;
    object.lightStrength[0] = record.lightX;
    object.lightStrength[1] = record.lightY;
    object.lightStrength[2] = record.lightZ;
    object.lightStrength[3] = record.strength;
    object.resolutionPenumbra[0] = static_cast<float>(m_width);
    object.resolutionPenumbra[1] = static_cast<float>(m_height);
    object.resolutionPenumbra[3] = record.penumbra;
    object.versions[0] = record.legacy ? 0.0f : 1.0f;
    object.versions[1] = static_cast<float>(record.pipelineId);
    object.versions[2] = static_cast<float>(record.resourceRevision);

    void* mapped = nullptr;
    check(vkMapMemory(m_device, transient.uboMemory, 0,
                      static_cast<VkDeviceSize>(sizeof(object)), 0, &mapped),
          "vkMapMemory(ubo)");
    std::memcpy(mapped, &object, sizeof(object));
    vkUnmapMemory(m_device, transient.uboMemory);

    transient.stagingBytes =
        static_cast<VkDeviceSize>(m_width) * m_height * 4u;
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = transient.stagingBytes;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(m_device, &stagingInfo, nullptr,
                         &transient.stagingBuffer),
          "vkCreateBuffer(staging)");
    ++m_liveTransientObjects;
    VkMemoryRequirements stagingRequirements{};
    vkGetBufferMemoryRequirements(m_device, transient.stagingBuffer,
                                  &stagingRequirements);
    VkMemoryAllocateInfo stagingAllocate{};
    stagingAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocate.allocationSize = stagingRequirements.size;
    stagingAllocate.memoryTypeIndex = chooseMemoryType(
        m_memoryProperties, stagingRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(m_device, &stagingAllocate, nullptr,
                           &transient.stagingMemory),
          "vkAllocateMemory(staging)");
    ++m_liveTransientObjects;
    check(vkBindBufferMemory(m_device, transient.stagingBuffer,
                             transient.stagingMemory, 0),
          "vkBindBufferMemory(staging)");

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {m_width, m_height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(m_device, &imageInfo, nullptr, &transient.colorImage),
          "vkCreateImage");
    ++m_liveTransientObjects;
    VkMemoryRequirements colorRequirements{};
    vkGetImageMemoryRequirements(m_device, transient.colorImage,
                                 &colorRequirements);
    VkMemoryAllocateInfo colorAllocate{};
    colorAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    colorAllocate.allocationSize = colorRequirements.size;
    colorAllocate.memoryTypeIndex = chooseMemoryType(
        m_memoryProperties, colorRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(m_device, &colorAllocate, nullptr,
                           &transient.colorMemory),
          "vkAllocateMemory(color)");
    ++m_liveTransientObjects;
    transient.colorMemoryBytes = colorRequirements.size;
    check(vkBindImageMemory(m_device, transient.colorImage,
                            transient.colorMemory, 0),
          "vkBindImageMemory");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = transient.colorImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    check(vkCreateImageView(m_device, &viewInfo, nullptr, &transient.colorView),
          "vkCreateImageView");
    ++m_liveTransientObjects;

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &transient.colorView;
    framebufferInfo.width = m_width;
    framebufferInfo.height = m_height;
    framebufferInfo.layers = 1;
    check(vkCreateFramebuffer(m_device, &framebufferInfo, nullptr,
                              &transient.framebuffer),
          "vkCreateFramebuffer");
    ++m_liveTransientObjects;

    VkDescriptorBufferInfo descriptorBuffer{};
    descriptorBuffer.buffer = transient.uboBuffer;
    descriptorBuffer.offset = 0;
    descriptorBuffer.range = transient.uboBytes;
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.pBufferInfo = &descriptorBuffer;
    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);

    check(vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX),
          "vkWaitForFences");
    check(vkResetCommandBuffer(m_commandBuffer, 0), "vkResetCommandBuffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(m_commandBuffer, &beginInfo),
          "vkBeginCommandBuffer");

    const VkClearValue clearValue{{{0.015f, 0.020f, 0.030f, 1.0f}}};
    VkRenderPassBeginInfo renderPassBegin{};
    renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBegin.renderPass = m_renderPass;
    renderPassBegin.framebuffer = transient.framebuffer;
    renderPassBegin.renderArea.offset = {0, 0};
    renderPassBegin.renderArea.extent = {m_width, m_height};
    renderPassBegin.clearValueCount = 1;
    renderPassBegin.pClearValues = &clearValue;
    vkCmdBeginRenderPass(m_commandBuffer, &renderPassBegin,
                         VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(m_width);
    viewport.height = static_cast<float>(m_height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = {m_width, m_height};
    vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_pipeline);
    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_descriptorSet, 0,
                            nullptr);
    vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(m_commandBuffer);

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = {m_width, m_height, 1};
    vkCmdCopyImageToBuffer(m_commandBuffer, transient.colorImage,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           transient.stagingBuffer, 1, &copyRegion);

    VkBufferMemoryBarrier hostBarrier{};
    hostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.buffer = transient.stagingBuffer;
    hostBarrier.offset = 0;
    hostBarrier.size = transient.stagingBytes;
    vkCmdPipelineBarrier(m_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                         &hostBarrier, 0, nullptr);
    check(vkEndCommandBuffer(m_commandBuffer), "vkEndCommandBuffer");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;
    check(vkResetFences(m_device, 1, &m_fence), "vkResetFences");
    check(vkQueueSubmit(m_queue, 1, &submitInfo, m_fence), "vkQueueSubmit");
    check(vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX),
          "vkWaitForFences(render)");

    check(vkMapMemory(m_device, transient.stagingMemory, 0, VK_WHOLE_SIZE, 0,
                      &mapped),
          "vkMapMemory(staging)");
    VkMappedMemoryRange mappedRange{};
    mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    mappedRange.memory = transient.stagingMemory;
    mappedRange.offset = 0;
    mappedRange.size = VK_WHOLE_SIZE;
    check(vkInvalidateMappedMemoryRanges(m_device, 1, &mappedRange),
          "vkInvalidateMappedMemoryRanges");

    RenderEvidence evidence;
    evidence.objectId = record.objectId;
    evidence.backend = "vulkan-offscreen";
    evidence.deviceName = m_deviceName;
    evidence.driverName = m_driverName;
    evidence.image.width = m_width;
    evidence.image.height = m_height;
    evidence.image.pixels.resize(static_cast<std::size_t>(m_width) * m_height *
                                 4u);
    std::memcpy(evidence.image.pixels.data(), mapped,
                evidence.image.pixels.size());
    vkUnmapMemory(m_device, transient.stagingMemory);

    const std::string imageHash = sha256Hex(evidence.image.pixels);
    const uint32_t imageCrc = crc32c(evidence.image.pixels.data(),
                                     evidence.image.pixels.size());
    const std::string uboHash = sha256Hex(&object, sizeof(object));
    const uint32_t uboCrc = crc32c(&object, sizeof(object));

    evidence.drawCall.commandBuffer = handleValue(m_commandBuffer);
    evidence.drawCall.pipeline = handleValue(m_pipeline);
    evidence.drawCall.pipelineLayout = handleValue(m_pipelineLayout);
    evidence.drawCall.descriptorSet = handleValue(m_descriptorSet);
    evidence.drawCall.topology = "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST";
    evidence.drawCall.descriptorBinding =
        "set=0,binding=0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,range=64";

    evidence.uniformBuffer.handle = handleValue(transient.uboBuffer);
    evidence.uniformBuffer.memoryHandle = handleValue(transient.uboMemory);
    evidence.uniformBuffer.memoryBytes =
        static_cast<uint64_t>(uboRequirements.size);
    evidence.uniformBuffer.contentCrc32c = uboCrc;
    evidence.uniformBuffer.kind = "VkBuffer";
    evidence.uniformBuffer.format = "std140 GpuShadowObject (64 bytes)";
    evidence.uniformBuffer.layoutBefore = "HOST_COHERENT mapped";
    evidence.uniformBuffer.layoutAfter = "VK_ACCESS_SHADER_READ_BIT";
    evidence.uniformBuffer.writeStage =
        "VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT";
    evidence.uniformBuffer.writeAccess = "VK_ACCESS_SHADER_READ_BIT";
    evidence.uniformBuffer.objectSha256 = uboHash;

    evidence.colorTarget.handle = handleValue(transient.colorImage);
    evidence.colorTarget.memoryHandle = handleValue(transient.colorMemory);
    evidence.colorTarget.memoryBytes =
        static_cast<uint64_t>(transient.colorMemoryBytes);
    evidence.colorTarget.contentCrc32c = imageCrc;
    evidence.colorTarget.kind = "VkImage";
    evidence.colorTarget.format = "VK_FORMAT_R8G8B8A8_UNORM";
    evidence.colorTarget.layoutBefore = "VK_IMAGE_LAYOUT_UNDEFINED";
    evidence.colorTarget.layoutAfter =
        "VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL";
    evidence.colorTarget.writeStage =
        "VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT";
    evidence.colorTarget.writeAccess = "VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT";
    evidence.colorTarget.objectSha256 = imageHash;

    evidence.framebuffer.renderPass = handleValue(m_renderPass);
    evidence.framebuffer.framebuffer = handleValue(transient.framebuffer);
    evidence.framebuffer.colorImageView = handleValue(transient.colorView);
    evidence.framebuffer.width = m_width;
    evidence.framebuffer.height = m_height;
    evidence.framebuffer.format = "VK_FORMAT_R8G8B8A8_UNORM";
    evidence.framebuffer.clearColor = "{0.015, 0.020, 0.030, 1.000}";
    evidence.framebuffer.contentSha256 = imageHash;
    evidence.framebuffer.contentCrc32c = std::to_string(imageCrc);
    return evidence;
}

}  // namespace lshc



