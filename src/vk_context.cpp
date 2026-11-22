#include "vk_context.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstring>
#include <iostream>
#include <vector>

namespace inst {
namespace {

VkBufferResource makeBuffer(const VulkanContext& context, VkDeviceSize size,
                            VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags properties,
                            uint32_t& memoryType) {
    VkBufferResource result;
    result.size = size;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    INST_VK_CHECK(vkCreateBuffer(context.device(), &bufferInfo, nullptr,
                                &result.buffer));
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context.device(), result.buffer,
                                  &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex =
        context.findMemoryType(requirements.memoryTypeBits, properties);
    memoryType = allocation.memoryTypeIndex;
    INST_VK_CHECK(vkAllocateMemory(context.device(), &allocation, nullptr,
                                  &result.memory));
    INST_VK_CHECK(vkBindBufferMemory(context.device(), result.buffer,
                                    result.memory, 0));
    return result;
}

} // namespace

VulkanContext::VulkanContext(uint32_t width, uint32_t height)
    : width_(width), height_(height) {
#if defined(GLFW_PLATFORM_NULL)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
#endif
    if (!glfwInit()) {
        throw RenderError("glfwInit failed");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window_ = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height),
                               "instance-selection-experiment", nullptr, nullptr);
    if (!window_) {
        const char* description = nullptr;
        const int code = glfwGetError(&description);
        const std::string message =
            "glfwCreateWindow failed, code=" + std::to_string(code) +
            (description ? std::string(": ") + description : std::string());
        glfwTerminate();
        throw RenderError(message);
    }

    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "instance-selection-experiment";
    application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application.pEngineName = "none";
    application.apiVersion = VK_API_VERSION_1_0;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions,
                                        glfwExtensions + glfwExtensionCount);

    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &application;
    instanceInfo.enabledExtensionCount =
        static_cast<uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames =
        extensions.empty() ? nullptr : extensions.data();
    INST_VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &instance_));

    uint32_t deviceCount = 0;
    INST_VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr));
    if (deviceCount == 0) {
        throw RenderError("no Vulkan physical device");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    INST_VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount,
                                            devices.data()));

    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices) {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount,
                                                 nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount,
                                                 families.data());
        int score = -1;
        uint32_t selectedFamily = 0;
        for (uint32_t family = 0; family < familyCount; ++family) {
            if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                score = 10;
                selectedFamily = family;
                break;
            }
        }
        if (score < 0) continue;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 100;
        } else if (properties.deviceType ==
                   VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            score += 40;
        } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
            score += 20;
        }
        if (score > bestScore) {
            bestScore = score;
            physicalDevice_ = candidate;
            queueFamily_ = selectedFamily;
            deviceName_ = properties.deviceName;
        }
    }
    if (!physicalDevice_) {
        throw RenderError("no graphics-capable Vulkan device");
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    INST_VK_CHECK(vkCreateDevice(physicalDevice_, &deviceInfo, nullptr,
                                &device_));
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

    VkCommandPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily_;
    INST_VK_CHECK(vkCreateCommandPool(device_, &poolInfo, nullptr,
                                     &commandPool_));
}

VulkanContext::~VulkanContext() {
    if (device_) vkDeviceWaitIdle(device_);
    if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
    if (device_) vkDestroyDevice(device_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

VkCommandBuffer VulkanContext::beginOneShot() const {
    VkCommandBufferAllocateInfo allocation{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = commandPool_;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    INST_VK_CHECK(vkAllocateCommandBuffers(device_, &allocation,
                                          &commandBuffer));
    VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    INST_VK_CHECK(vkBeginCommandBuffer(commandBuffer, &begin));
    return commandBuffer;
}

void VulkanContext::endAndWait(VkCommandBuffer commandBuffer) const {
    INST_VK_CHECK(vkEndCommandBuffer(commandBuffer));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    INST_VK_CHECK(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE));
    INST_VK_CHECK(vkQueueWaitIdle(queue_));
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

VkShaderModule VulkanContext::loadShader(const uint32_t* words,
                                         size_t wordCount) const {
    VkShaderModuleCreateInfo createInfo{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = wordCount * sizeof(uint32_t);
    createInfo.pCode = words;
    VkShaderModule module = VK_NULL_HANDLE;
    INST_VK_CHECK(vkCreateShaderModule(device_, &createInfo, nullptr,
                                      &module));
    return module;
}

uint32_t VulkanContext::findMemoryType(
        uint32_t typeBits, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) != 0 &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw RenderError("no suitable Vulkan memory type");
}

VkBufferResource createBufferResource(const VulkanContext& context,
                                      VkDeviceSize size,
                                      VkBufferUsageFlags usage,
                                      VkMemoryPropertyFlags properties) {
    uint32_t ignored = 0;
    return makeBuffer(context, size, usage, properties, ignored);
}

void destroyBufferResource(const VulkanContext& context,
                           VkBufferResource& resource) {
    if (resource.buffer) {
        vkDestroyBuffer(context.device(), resource.buffer, nullptr);
    }
    if (resource.memory) {
        vkFreeMemory(context.device(), resource.memory, nullptr);
    }
    resource = {};
}

void uploadBuffer(const VulkanContext& context,
                  const VkBufferResource& destination,
                  const void* source, VkDeviceSize size) {
    VkBufferResource staging = createBufferResource(
        context, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    INST_VK_CHECK(vkMapMemory(context.device(), staging.memory, 0, size, 0,
                             &mapped));
    std::memcpy(mapped, source, static_cast<size_t>(size));
    vkUnmapMemory(context.device(), staging.memory);

    VkCommandBuffer commandBuffer = context.beginOneShot();
    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(commandBuffer, staging.buffer, destination.buffer, 1,
                    &region);
    context.endAndWait(commandBuffer);
    destroyBufferResource(context, staging);
}

} // namespace inst
