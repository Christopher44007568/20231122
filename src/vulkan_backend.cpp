#include "vulkan_backend.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

namespace vegd {
namespace {

template <typename T>
T as(void* value) {
    return reinterpret_cast<T>(value);
}

template <typename T>
T* out(void* value) {
    return reinterpret_cast<T*>(value);
}

const char* resultName(VkResult result) {
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
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        default:
            return "VK_ERROR_OTHER";
    }
}

bool check(VkResult result, const char* operation, std::string& error) {
    if (result == VK_SUCCESS) {
        return true;
    }
    error = std::string(operation) + " failed: " + resultName(result);
    return false;
}

std::vector<char> readFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
}

}  // namespace

VulkanBackend::~VulkanBackend() {
    shutdown();
}

bool VulkanBackend::loadShader(std::string& error) {
    const std::string candidates[] = {
        std::string(VEGDIST_SHADER_DIR) + "/vegetation.comp.spv",
        std::string(VEGDIST_SOURCE_DIR) + "/shaders/vegetation.comp.spv",
        "shaders/vegetation.comp.spv",
    };
    std::vector<char> code;
    std::string selected;
    for (const std::string& candidate : candidates) {
        code = readFile(candidate);
        if (!code.empty()) {
            selected = candidate;
            break;
        }
    }
    if (code.empty() || (code.size() % sizeof(uint32_t)) != 0) {
        error = "cannot load a valid vegetation.comp.spv";
        return false;
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    return check(vkCreateShaderModule(as<VkDevice>(device_), &info, nullptr,
                                      out<VkShaderModule>(&shaderModule_)),
                 ("vkCreateShaderModule(" + selected + ")").c_str(), error);
}

bool VulkanBackend::initialize(std::string& error) {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "procedural-vegetation";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "vegd";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &app;
    if (!check(vkCreateInstance(&instanceInfo, nullptr,
                                out<VkInstance>(&instance_)),
               "vkCreateInstance", error)) {
        return false;
    }

    uint32_t physicalCount = 0;
    if (!check(vkEnumeratePhysicalDevices(as<VkInstance>(instance_),
                                          &physicalCount, nullptr),
               "vkEnumeratePhysicalDevices", error) ||
        physicalCount == 0) {
        if (error.empty()) {
            error = "no Vulkan physical device";
        }
        return false;
    }
    std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
    if (!check(vkEnumeratePhysicalDevices(as<VkInstance>(instance_),
                                          &physicalCount, physicalDevices.data()),
               "vkEnumeratePhysicalDevices(list)", error)) {
        return false;
    }

    uint32_t queueFamily = std::numeric_limits<uint32_t>::max();
    VkPhysicalDeviceProperties selectedProperties{};
    VkPhysicalDeviceMemoryProperties selectedMemory{};
    VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;
    int bestScore = -1;
    for (VkPhysicalDevice candidate : physicalDevices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount,
                                                 families.data());
        uint32_t found = std::numeric_limits<uint32_t>::max();
        for (uint32_t i = 0; i < familyCount; ++i) {
            if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
                found = i;
                break;
            }
        }
        if (found == std::numeric_limits<uint32_t>::max()) {
            continue;
        }
        int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
                        ? 300
                        : properties.deviceType ==
                                  VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
                              ? 200
                              : properties.deviceType ==
                                        VK_PHYSICAL_DEVICE_TYPE_CPU
                                    ? 100
                                    : 50;
        if (score > bestScore) {
            bestScore = score;
            selectedDevice = candidate;
            queueFamily = found;
            selectedProperties = properties;
            vkGetPhysicalDeviceMemoryProperties(candidate, &selectedMemory);
        }
    }
    if (selectedDevice == VK_NULL_HANDLE) {
        error = "no Vulkan compute queue family";
        return false;
    }
    physicalDevice_ = selectedDevice;
    deviceName_ = selectedProperties.deviceName;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    if (!check(vkCreateDevice(selectedDevice, &deviceInfo, nullptr,
                              out<VkDevice>(&device_)),
               "vkCreateDevice", error)) {
        return false;
    }
    vkGetDeviceQueue(as<VkDevice>(device_), queueFamily, 0,
                     out<VkQueue>(&computeQueue_));

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    if (!check(vkCreateCommandPool(as<VkDevice>(device_), &poolInfo, nullptr,
                                   out<VkCommandPool>(&commandPool_)),
               "vkCreateCommandPool", error)) {
        return false;
    }
    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = as<VkCommandPool>(commandPool_);
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    if (!check(vkAllocateCommandBuffers(as<VkDevice>(device_), &commandInfo,
                                        out<VkCommandBuffer>(&commandBuffer_)),
               "vkAllocateCommandBuffers", error)) {
        return false;
    }
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (!check(vkCreateFence(as<VkDevice>(device_), &fenceInfo, nullptr,
                             out<VkFence>(&fence_)),
               "vkCreateFence", error)) {
        return false;
    }
    if (!loadShader(error)) {
        return false;
    }
    if (!createPipeline(error)) {
        return false;
    }
    initialized_ = true;
    return true;
}

bool VulkanBackend::createPipeline(std::string& error) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (!check(vkCreateDescriptorSetLayout(as<VkDevice>(device_), &layoutInfo,
                                           nullptr,
                                           out<VkDescriptorSetLayout>(
                                               &descriptorSetLayout_)),
               "vkCreateDescriptorSetLayout", error)) {
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (!check(vkCreateDescriptorPool(as<VkDevice>(device_), &poolInfo, nullptr,
                                      out<VkDescriptorPool>(&descriptorPool_)),
               "vkCreateDescriptorPool", error)) {
        return false;
    }
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = as<VkDescriptorPool>(descriptorPool_);
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = out<VkDescriptorSetLayout>(&descriptorSetLayout_);
    if (!check(vkAllocateDescriptorSets(as<VkDevice>(device_), &setInfo,
                                        out<VkDescriptorSet>(&descriptorSet_)),
               "vkAllocateDescriptorSets", error)) {
        return false;
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts =
        out<VkDescriptorSetLayout>(&descriptorSetLayout_);
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (!check(vkCreatePipelineLayout(as<VkDevice>(device_),
                                      &pipelineLayoutInfo, nullptr,
                                      out<VkPipelineLayout>(&pipelineLayout_)),
               "vkCreatePipelineLayout", error)) {
        return false;
    }

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = as<VkShaderModule>(shaderModule_);
    stage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = as<VkPipelineLayout>(pipelineLayout_);
    if (!check(vkCreateComputePipelines(as<VkDevice>(device_), VK_NULL_HANDLE, 1,
                                        &pipelineInfo, nullptr,
                                        out<VkPipeline>(&pipeline_)),
               "vkCreateComputePipelines", error)) {
        return false;
    }
    return true;
}

bool VulkanBackend::ensureCapacity(uint32_t capacity, std::string& error) {
    if (capacity == 0) {
        error = "zero Vulkan instance capacity";
        return false;
    }
    if (outputBuffer_ != nullptr && outputCapacity_ >= capacity) {
        return true;
    }
    if (outputBuffer_ != nullptr) {
        vkUnmapMemory(as<VkDevice>(device_), as<VkDeviceMemory>(outputMemory_));
        mappedOutput_ = nullptr;
        vkDestroyBuffer(as<VkDevice>(device_), as<VkBuffer>(outputBuffer_),
                        nullptr);
        outputBuffer_ = nullptr;
        vkFreeMemory(as<VkDevice>(device_), as<VkDeviceMemory>(outputMemory_),
                     nullptr);
        outputMemory_ = nullptr;
    }

    const VkDeviceSize size =
        static_cast<VkDeviceSize>(16) +
        static_cast<VkDeviceSize>(capacity) * sizeof(Instance);
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(vkCreateBuffer(as<VkDevice>(device_), &bufferInfo, nullptr,
                              out<VkBuffer>(&outputBuffer_)),
               "vkCreateBuffer", error)) {
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(as<VkDevice>(device_),
                                  as<VkBuffer>(outputBuffer_), &requirements);
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(as<VkPhysicalDevice>(physicalDevice_),
                                        &memory);
    uint32_t memoryType = std::numeric_limits<uint32_t>::max();
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        const VkMemoryPropertyFlags flags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((requirements.memoryTypeBits & (1u << i)) != 0 &&
            (memory.memoryTypes[i].propertyFlags & flags) == flags) {
            memoryType = i;
            break;
        }
    }
    if (memoryType == std::numeric_limits<uint32_t>::max()) {
        error = "no host-visible coherent Vulkan memory type";
        return false;
    }
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    if (!check(vkAllocateMemory(as<VkDevice>(device_), &allocateInfo, nullptr,
                                out<VkDeviceMemory>(&outputMemory_)),
               "vkAllocateMemory", error)) {
        return false;
    }
    if (!check(vkBindBufferMemory(as<VkDevice>(device_),
                                  as<VkBuffer>(outputBuffer_),
                                  as<VkDeviceMemory>(outputMemory_), 0),
               "vkBindBufferMemory", error)) {
        return false;
    }
    if (!check(vkMapMemory(as<VkDevice>(device_),
                           as<VkDeviceMemory>(outputMemory_), 0,
                           VK_WHOLE_SIZE, 0, &mappedOutput_),
               "vkMapMemory", error)) {
        return false;
    }
    std::memset(mappedOutput_, 0, static_cast<size_t>(size));

    VkDescriptorBufferInfo bufferDescriptor{};
    bufferDescriptor.buffer = as<VkBuffer>(outputBuffer_);
    bufferDescriptor.offset = 0;
    bufferDescriptor.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = as<VkDescriptorSet>(descriptorSet_);
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferDescriptor;
    vkUpdateDescriptorSets(as<VkDevice>(device_), 1, &write, 0, nullptr);
    outputBufferSize_ = size;
    outputCapacity_ = capacity;
    return true;
}

bool VulkanBackend::generateTile(const Params& params, uint32_t tile,
                                 uint32_t capacity, TileData& result,
                                 std::string& error) {
    if (!initialized_) {
        error = "Vulkan backend is not initialized";
        return false;
    }
    if (!ensureCapacity(capacity, error)) {
        return false;
    }
    if (!check(vkResetCommandBuffer(as<VkCommandBuffer>(commandBuffer_), 0),
               "vkResetCommandBuffer", error)) {
        return false;
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(vkBeginCommandBuffer(as<VkCommandBuffer>(commandBuffer_), &begin),
               "vkBeginCommandBuffer", error)) {
        return false;
    }
    vkCmdBindPipeline(as<VkCommandBuffer>(commandBuffer_),
                      VK_PIPELINE_BIND_POINT_COMPUTE,
                      as<VkPipeline>(pipeline_));
    const VkDescriptorSet descriptor = as<VkDescriptorSet>(descriptorSet_);
    vkCmdBindDescriptorSets(as<VkCommandBuffer>(commandBuffer_),
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            as<VkPipelineLayout>(pipelineLayout_), 0, 1,
                            &descriptor, 0, nullptr);
    const PushConstants push = makePushConstants(params, tile, capacity);
    vkCmdPushConstants(as<VkCommandBuffer>(commandBuffer_),
                       as<VkPipelineLayout>(pipelineLayout_),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    const uint32_t groups = (capacity + 255u) / 256u;
    vkCmdDispatch(as<VkCommandBuffer>(commandBuffer_), groups, 1, 1);
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(as<VkCommandBuffer>(commandBuffer_),
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr,
                         0, nullptr);
    if (!check(vkEndCommandBuffer(as<VkCommandBuffer>(commandBuffer_)),
               "vkEndCommandBuffer", error)) {
        return false;
    }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    const VkCommandBuffer command = as<VkCommandBuffer>(commandBuffer_);
    submit.pCommandBuffers = &command;
    const VkFence fence = as<VkFence>(fence_);
    if (!check(vkResetFences(as<VkDevice>(device_), 1, &fence),
               "vkResetFences", error) ||
        !check(vkQueueSubmit(as<VkQueue>(computeQueue_), 1, &submit,
                             as<VkFence>(fence_)),
               "vkQueueSubmit", error) ||
        !check(vkWaitForFences(as<VkDevice>(device_), 1, &fence, VK_TRUE,
                               std::numeric_limits<uint64_t>::max()),
               "vkWaitForFences", error)) {
        return false;
    }

    const auto* values = static_cast<const float*>(mappedOutput_);
    const uint32_t count = *reinterpret_cast<const uint32_t*>(mappedOutput_);
    if (count > capacity) {
        error = "compute shader returned count above capacity";
        return false;
    }
    std::vector<Instance> instances;
    instances.reserve(count);
    const size_t floatBase = 4;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t offset = floatBase + static_cast<size_t>(i) * 4u;
        Instance instance;
        instance.x = values[offset + 0u];
        instance.y = values[offset + 1u];
        instance.z = values[offset + 2u];
        instance.kind = static_cast<uint32_t>(values[offset + 3u]);
        instances.push_back(instance);
    }
    result = makeTileFromInstances(params, tile, count, instances);
    (void)outputBufferSize_;
    return true;
}

void VulkanBackend::shutdown() {
    initialized_ = false;
    if (device_ == nullptr) {
        if (instance_ != nullptr) {
            vkDestroyInstance(as<VkInstance>(instance_), nullptr);
            instance_ = nullptr;
        }
        return;
    }
    vkDeviceWaitIdle(as<VkDevice>(device_));
    if (mappedOutput_ != nullptr && outputMemory_ != nullptr) {
        vkUnmapMemory(as<VkDevice>(device_), as<VkDeviceMemory>(outputMemory_));
        mappedOutput_ = nullptr;
    }
    if (outputBuffer_ != nullptr) {
        vkDestroyBuffer(as<VkDevice>(device_), as<VkBuffer>(outputBuffer_),
                        nullptr);
        outputBuffer_ = nullptr;
    }
    if (outputMemory_ != nullptr) {
        vkFreeMemory(as<VkDevice>(device_), as<VkDeviceMemory>(outputMemory_),
                     nullptr);
        outputMemory_ = nullptr;
    }
    if (pipeline_ != nullptr) {
        vkDestroyPipeline(as<VkDevice>(device_), as<VkPipeline>(pipeline_),
                          nullptr);
        pipeline_ = nullptr;
    }
    if (pipelineLayout_ != nullptr) {
        vkDestroyPipelineLayout(as<VkDevice>(device_),
                                as<VkPipelineLayout>(pipelineLayout_), nullptr);
        pipelineLayout_ = nullptr;
    }
    if (descriptorPool_ != nullptr) {
        vkDestroyDescriptorPool(as<VkDevice>(device_),
                                as<VkDescriptorPool>(descriptorPool_), nullptr);
        descriptorPool_ = nullptr;
        descriptorSet_ = nullptr;
    }
    if (descriptorSetLayout_ != nullptr) {
        vkDestroyDescriptorSetLayout(as<VkDevice>(device_),
                                     as<VkDescriptorSetLayout>(
                                         descriptorSetLayout_),
                                     nullptr);
        descriptorSetLayout_ = nullptr;
    }
    if (shaderModule_ != nullptr) {
        vkDestroyShaderModule(as<VkDevice>(device_),
                              as<VkShaderModule>(shaderModule_), nullptr);
        shaderModule_ = nullptr;
    }
    if (fence_ != nullptr) {
        vkDestroyFence(as<VkDevice>(device_), as<VkFence>(fence_), nullptr);
        fence_ = nullptr;
    }
    if (commandPool_ != nullptr) {
        vkDestroyCommandPool(as<VkDevice>(device_),
                             as<VkCommandPool>(commandPool_), nullptr);
        commandPool_ = nullptr;
        commandBuffer_ = nullptr;
    }
    vkDestroyDevice(as<VkDevice>(device_), nullptr);
    device_ = nullptr;
    if (instance_ != nullptr) {
        vkDestroyInstance(as<VkInstance>(instance_), nullptr);
        instance_ = nullptr;
    }
}

}  // namespace vegd
