#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <vulkan/vulkan.h>

#include "scene.h"
#include "sha256.h"

namespace {

float saturate(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

uint8_t toByte(float linear) {
    float mapped = linear / (1.0f + std::max(0.0f, linear));
    float gamma = std::pow(saturate(mapped), 1.0f / 2.2f);
    return static_cast<uint8_t>(std::lround(saturate(gamma) * 255.0f));
}

class CpuRenderer final : public Renderer {
public:
    bool renderFrame(const RenderParams& params, Image& output, std::string& error) override {
        (void)error;
        output = Image(static_cast<int>(params.width), static_cast<int>(params.height));
        for (uint32_t y = 0; y < params.height; ++y) {
            for (uint32_t x = 0; x < params.width; ++x) {
                Vec3f color = shadeBokehPixel(static_cast<float>(x), static_cast<float>(y), params);
                size_t offset = (static_cast<size_t>(y) * params.width + x) * 4;
                output.rgba[offset] = toByte(color.x);
                output.rgba[offset + 1] = toByte(color.y);
                output.rgba[offset + 2] = toByte(color.z);
                output.rgba[offset + 3] = 255;
            }
        }
        return true;
    }

    Json diagnostics() const override {
        Json value = Json::object();
        value.set("api", "cpu-reference-c++17");
        return value;
    }

    std::string backendName() const override { return "cpu-reference"; }
};

std::string vkVersionString(uint32_t version) {
    std::ostringstream out;
    out << VK_VERSION_MAJOR(version) << '.' << VK_VERSION_MINOR(version) << '.'
        << VK_VERSION_PATCH(version);
    return out.str();
}

std::string vkResultString(VkResult result) {
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
        default: return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
    }
}

void checkVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed: " + vkResultString(result));
    }
}

std::string shaderPath() {
    std::string compiled = BOKEH_SHADER_PATH;
    std::ifstream direct(compiled, std::ios::binary);
    if (direct.good()) return compiled;
    return joinPath(executableDirectory(), "shaders/bokeh.comp.spv");
}

struct GpuPushConstants {
    uint32_t width;
    uint32_t height;
    uint32_t tileY;
    uint32_t tileHeight;
    uint32_t frameIndex;
    uint32_t totalFrames;
    uint32_t deviceIndex;
    uint32_t seed;
    float timeSeconds;
    float focusDepth;
    float aperture;
    float phase;
    float clockOffsetMs;
    float clockDriftPpm;
    float exposure;
    float padding;
};

static_assert(sizeof(GpuPushConstants) == 64, "push constant layout must remain stable");

class VulkanRenderer final : public Renderer {
public:
    VulkanRenderer(const Config& config, const RenderPlan& plan)
        : config_(config), plan_(plan) {
        try {
            initialize();
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~VulkanRenderer() override {
        cleanup();
    }

    bool renderFrame(const RenderParams& params, Image& output, std::string& error) override {
        try {
            output = Image(static_cast<int>(params.width), static_cast<int>(params.height));
            uint32_t bandHeight = std::min<uint32_t>(plan_.bandHeight, params.height);
            for (uint32_t bandStart = 0; bandStart < params.height; bandStart += bandHeight) {
                uint32_t currentHeight = std::min(bandHeight, params.height - bandStart);
                GpuPushConstants constants{};
                constants.width = params.width;
                constants.height = params.height;
                constants.tileY = bandStart;
                constants.tileHeight = currentHeight;
                constants.frameIndex = params.frameIndex;
                constants.totalFrames = params.totalFrames;
                constants.deviceIndex = params.deviceIndex;
                constants.seed = params.seed;
                constants.timeSeconds = params.timeSeconds;
                constants.focusDepth = params.focusDepth;
                constants.aperture = params.aperture;
                constants.phase = params.phase;
                constants.clockOffsetMs = params.clockOffsetMs;
                constants.clockDriftPpm = params.clockDriftPpm;
                constants.exposure = params.exposure;
                constants.padding = params.padding;
                dispatchBand(constants, output, bandStart, currentHeight);
            }
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }

    Json diagnostics() const override {
        return diagnostics_;
    }

    std::string backendName() const override { return "vulkan-compute"; }

private:
    Config config_;
    RenderPlan plan_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkBuffer outputBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory outputMemory_ = VK_NULL_HANDLE;
    void* mappedOutput_ = nullptr;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    uint32_t maxBandHeight_ = 0;
    Json diagnostics_ = Json::object();

    void initialize();
    void createBuffer();
    void createPipeline();
    void dispatchBand(const GpuPushConstants& constants, Image& output,
                      uint32_t bandStart, uint32_t bandHeight);
    void cleanup();
};

void VulkanRenderer::initialize() {
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "bokeh-pipeline";
    application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application.pEngineName = "analytic-bokeh-compute";
    application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceCreate{};
    instanceCreate.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreate.pApplicationInfo = &application;
    checkVk(vkCreateInstance(&instanceCreate, nullptr, &instance_), "vkCreateInstance");

    uint32_t physicalCount = 0;
    checkVk(vkEnumeratePhysicalDevices(instance_, &physicalCount, nullptr),
            "vkEnumeratePhysicalDevices(count)");
    if (physicalCount == 0) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
    checkVk(vkEnumeratePhysicalDevices(instance_, &physicalCount, physicalDevices.data()),
            "vkEnumeratePhysicalDevices");
    uint32_t queueTimestampValidBits = 0;
    for (VkPhysicalDevice candidate : physicalDevices) {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (uint32_t family = 0; family < familyCount; ++family) {
            if ((families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
                physicalDevice_ = candidate;
                queueFamily_ = family;
                queueTimestampValidBits = families[family].timestampValidBits;
                break;
            }
        }
        if (physicalDevice_ != VK_NULL_HANDLE) break;
    }
    if (physicalDevice_ == VK_NULL_HANDLE) throw std::runtime_error("no Vulkan compute queue");

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
    uint64_t heapBytes = 0;
    for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; ++i) {
        if ((memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
            heapBytes = std::max<uint64_t>(heapBytes, memoryProperties.memoryHeaps[i].size);
        }
    }
    if (heapBytes == 0 && memoryProperties.memoryHeapCount > 0) {
        heapBytes = memoryProperties.memoryHeaps[0].size;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueCreate{};
    queueCreate.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreate.queueFamilyIndex = queueFamily_;
    queueCreate.queueCount = 1;
    queueCreate.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceCreate{};
    deviceCreate.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreate.queueCreateInfoCount = 1;
    deviceCreate.pQueueCreateInfos = &queueCreate;
    checkVk(vkCreateDevice(physicalDevice_, &deviceCreate, nullptr, &device_),
            "vkCreateDevice");
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

    maxBandHeight_ = std::min<uint32_t>(plan_.bandHeight, static_cast<uint32_t>(config_.height));
    if (maxBandHeight_ == 0) throw std::runtime_error("render plan has zero band height");

    createBuffer();
    createPipeline();

    VkCommandPoolCreateInfo poolCreate{};
    poolCreate.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCreate.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCreate.queueFamilyIndex = queueFamily_;
    checkVk(vkCreateCommandPool(device_, &poolCreate, nullptr, &commandPool_),
            "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandAllocate{};
    commandAllocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandAllocate.commandPool = commandPool_;
    commandAllocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocate.commandBufferCount = 1;
    checkVk(vkAllocateCommandBuffers(device_, &commandAllocate, &commandBuffer_),
            "vkAllocateCommandBuffers");

    VkDescriptorPoolSize descriptorSize{};
    descriptorSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo descriptorPoolCreate{};
    descriptorPoolCreate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreate.maxSets = 1;
    descriptorPoolCreate.poolSizeCount = 1;
    descriptorPoolCreate.pPoolSizes = &descriptorSize;
    checkVk(vkCreateDescriptorPool(device_, &descriptorPoolCreate, nullptr, &descriptorPool_),
            "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo descriptorAllocate{};
    descriptorAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorAllocate.descriptorPool = descriptorPool_;
    descriptorAllocate.descriptorSetCount = 1;
    descriptorAllocate.pSetLayouts = &descriptorSetLayout_;
    checkVk(vkAllocateDescriptorSets(device_, &descriptorAllocate, &descriptorSet_),
            "vkAllocateDescriptorSets");

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = outputBuffer_;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet_;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrite.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);

    VkFenceCreateInfo fenceCreate{};
    fenceCreate.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    checkVk(vkCreateFence(device_, &fenceCreate, nullptr, &fence_), "vkCreateFence");

    diagnostics_ = Json::object();
    diagnostics_.set("api", "vulkan-compute");
    diagnostics_.set("api_version", vkVersionString(properties.apiVersion));
    diagnostics_.set("device_name", properties.deviceName);
    diagnostics_.set("device_type", static_cast<int>(properties.deviceType));
    diagnostics_.set("vendor_id", static_cast<int64_t>(properties.vendorID));
    diagnostics_.set("device_id", static_cast<int64_t>(properties.deviceID));
    diagnostics_.set("driver_version", static_cast<int64_t>(properties.driverVersion));
    diagnostics_.set("queue_family_index", static_cast<int64_t>(queueFamily_));
    diagnostics_.set("queue_timestamp_valid_bits",
                     static_cast<int64_t>(queueTimestampValidBits));
    diagnostics_.set("memory_heap_bytes", static_cast<int64_t>(heapBytes));
    diagnostics_.set("max_compute_workgroup_count_x",
                     static_cast<int64_t>(properties.limits.maxComputeWorkGroupCount[0]));
    diagnostics_.set("max_storage_buffer_range",
                     static_cast<int64_t>(properties.limits.maxStorageBufferRange));
    diagnostics_.set("shader_sha256", sha256File(shaderPath()));
    diagnostics_.set("band_height", static_cast<int64_t>(maxBandHeight_));
}

void VulkanRenderer::createBuffer() {
    VkDeviceSize bytes = static_cast<VkDeviceSize>(config_.width) * maxBandHeight_ * 4ULL;
    VkBufferCreateInfo bufferCreate{};
    bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreate.size = bytes;
    bufferCreate.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferCreate.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateBuffer(device_, &bufferCreate, nullptr, &outputBuffer_), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, outputBuffer_, &requirements);
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
        throw std::runtime_error("no host-visible Vulkan memory type");
    }
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType;
    checkVk(vkAllocateMemory(device_, &allocate, nullptr, &outputMemory_),
            "vkAllocateMemory(output)");
    checkVk(vkBindBufferMemory(device_, outputBuffer_, outputMemory_, 0),
            "vkBindBufferMemory");
    checkVk(vkMapMemory(device_, outputMemory_, 0, VK_WHOLE_SIZE, 0, &mappedOutput_),
            "vkMapMemory");
}

void VulkanRenderer::createPipeline() {
    std::vector<uint8_t> shaderBytes = readBinaryFile(shaderPath());
    if (shaderBytes.empty() || shaderBytes.size() % 4 != 0) {
        throw std::runtime_error("invalid SPIR-V shader: " + shaderPath());
    }
    VkShaderModuleCreateInfo shaderCreate{};
    shaderCreate.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderCreate.codeSize = shaderBytes.size();
    shaderCreate.pCode = reinterpret_cast<const uint32_t*>(shaderBytes.data());
    checkVk(vkCreateShaderModule(device_, &shaderCreate, nullptr, &shaderModule_),
            "vkCreateShaderModule");

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo layoutCreate{};
    layoutCreate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCreate.bindingCount = 1;
    layoutCreate.pBindings = &binding;
    checkVk(vkCreateDescriptorSetLayout(device_, &layoutCreate, nullptr, &descriptorSetLayout_),
            "vkCreateDescriptorSetLayout");

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(GpuPushConstants);
    VkPipelineLayoutCreateInfo pipelineLayoutCreate{};
    pipelineLayoutCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreate.setLayoutCount = 1;
    pipelineLayoutCreate.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutCreate.pushConstantRangeCount = 1;
    pipelineLayoutCreate.pPushConstantRanges = &pushRange;
    checkVk(vkCreatePipelineLayout(device_, &pipelineLayoutCreate, nullptr, &pipelineLayout_),
            "vkCreatePipelineLayout");

    VkComputePipelineCreateInfo pipelineCreate{};
    pipelineCreate.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineCreate.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineCreate.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineCreate.stage.module = shaderModule_;
    pipelineCreate.stage.pName = "main";
    pipelineCreate.layout = pipelineLayout_;
    checkVk(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineCreate, nullptr, &pipeline_),
            "vkCreateComputePipelines");
}

void VulkanRenderer::dispatchBand(const GpuPushConstants& constants, Image& output,
                                  uint32_t bandStart, uint32_t bandHeight) {
    checkVk(vkResetCommandBuffer(commandBuffer_, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(commandBuffer_, &begin), "vkBeginCommandBuffer");
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                            0, 1, &descriptorSet_, 0, nullptr);
    vkCmdPushConstants(commandBuffer_, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(constants), &constants);
    uint32_t groupsX = (constants.width + 7U) / 8U;
    uint32_t groupsY = (bandHeight + 7U) / 8U;
    vkCmdDispatch(commandBuffer_, groupsX, groupsY, 1);
    checkVk(vkEndCommandBuffer(commandBuffer_), "vkEndCommandBuffer");

    checkVk(vkResetFences(device_, 1, &fence_), "vkResetFences");
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer_;
    checkVk(vkQueueSubmit(queue_, 1, &submit, fence_), "vkQueueSubmit");
    checkVk(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "vkWaitForFences");

    size_t rowBytes = static_cast<size_t>(constants.width) * 4;
    const auto* source = static_cast<const uint8_t*>(mappedOutput_);
    for (uint32_t row = 0; row < bandHeight; ++row) {
        std::memcpy(output.rgba.data() + static_cast<size_t>(bandStart + row) * rowBytes,
                    source + static_cast<size_t>(row) * rowBytes, rowBytes);
    }
}

void VulkanRenderer::cleanup() {
    if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
    if (mappedOutput_ != nullptr && outputMemory_ != VK_NULL_HANDLE) {
        vkUnmapMemory(device_, outputMemory_);
        mappedOutput_ = nullptr;
    }
    if (fence_ != VK_NULL_HANDLE) vkDestroyFence(device_, fence_, nullptr);
    if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, commandPool_, nullptr);
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (shaderModule_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, shaderModule_, nullptr);
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
    }
    if (outputBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, outputBuffer_, nullptr);
    if (outputMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, outputMemory_, nullptr);
    if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    fence_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    shaderModule_ = VK_NULL_HANDLE;
    descriptorSetLayout_ = VK_NULL_HANDLE;
    outputBuffer_ = VK_NULL_HANDLE;
    outputMemory_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    instance_ = VK_NULL_HANDLE;
}

void appendEvent(std::vector<ResourceEvent>& events, const std::string& stage,
                 const std::string& severity, const std::string& code,
                 uint64_t requested, uint64_t available, const std::string& action,
                 const std::string& detail) {
    ResourceEvent event;
    event.sequence = static_cast<int>(events.size());
    event.stage = stage;
    event.severity = severity;
    event.code = code;
    event.requestedBytes = requested;
    event.availableBytes = available;
    event.action = action;
    event.detail = detail;
    events.push_back(std::move(event));
}

}  // namespace

uint64_t estimateTransientBytes(const Config& config) {
    uint64_t pixels = static_cast<uint64_t>(config.width) * static_cast<uint64_t>(config.height);
    return pixels * 4ULL * 3ULL;
}

RenderPlan makeRenderPlan(const Config& config, uint64_t budgetBytes) {
    RenderPlan plan;
    plan.requestedBackend = config.backend;
    plan.budgetBytes = budgetBytes;
    plan.fullTransientBytes = estimateTransientBytes(config);
    if (config.backend == "cpu") {
        plan.effectiveBackend = "cpu-reference";
        plan.reason = "CPU backend explicitly requested";
        return plan;
    }
    if (budgetBytes >= plan.fullTransientBytes) {
        plan.effectiveBackend = "vulkan-full";
        plan.bandHeight = static_cast<uint32_t>(config.height);
        plan.bandCount = 1;
        plan.reason = "full transient allocation fits the configured budget";
        return plan;
    }
    uint64_t bytesPerRow = static_cast<uint64_t>(config.width) * 4ULL;
    uint64_t rows = bytesPerRow == 0 ? 0 : budgetBytes / bytesPerRow;
    if (rows >= 1) {
        plan.effectiveBackend = "vulkan-banded";
        plan.degraded = true;
        plan.bandHeight = static_cast<uint32_t>(std::min<uint64_t>(rows, config.height));
        plan.bandCount = (static_cast<uint32_t>(config.height) + plan.bandHeight - 1) /
                         plan.bandHeight;
        plan.reason = "transient buffer exceeds budget; split into host-mapped bands";
        return plan;
    }
    plan.effectiveBackend = "cpu-reference";
    plan.degraded = true;
    plan.reason = "budget cannot hold one pixel row; using CPU reference path";
    return plan;
}

std::unique_ptr<Renderer> createRenderer(const Config& config,
                                         RenderPlan& plan,
                                         std::vector<ResourceEvent>& events) {
    if (plan.effectiveBackend == "cpu-reference") {
        appendEvent(events, "plan", "warning", "gpu_capacity_degraded",
                    plan.fullTransientBytes, plan.budgetBytes, plan.effectiveBackend,
                    plan.reason);
        return std::make_unique<CpuRenderer>();
    }

    if (plan.degraded) {
        appendEvent(events, "plan", "warning", "gpu_capacity_degraded",
                    plan.fullTransientBytes, plan.budgetBytes, plan.effectiveBackend,
                    plan.reason);
    }
    try {
        auto renderer = std::make_unique<VulkanRenderer>(config, plan);
        return renderer;
    } catch (const std::exception& exception) {
        plan.effectiveBackend = "cpu-reference";
        plan.degraded = true;
        plan.reason = std::string("Vulkan initialization failed: ") + exception.what();
        appendEvent(events, "init", "error", "vulkan_init_failed",
                    plan.fullTransientBytes, plan.budgetBytes, "cpu-reference",
                    exception.what());
        return std::make_unique<CpuRenderer>();
    }
}

std::unique_ptr<Renderer> createCpuRenderer() {
    return std::make_unique<CpuRenderer>();
}

