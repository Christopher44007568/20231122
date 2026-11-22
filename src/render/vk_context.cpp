#include "render/vk_context.h"

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace ssor {
namespace {

bool vkOk(VkResult result, const char* what, std::string* error) {
    if (result == VK_SUCCESS) return true;
    if (error) {
        *error = std::string(what) + " failed: " + vulkanResultName(result);
    }
    return false;
}

bool layerAvailable(const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const VkLayerProperties& layer : layers) {
        if (std::strcmp(layer.layerName, name) == 0) return true;
    }
    return false;
}

bool hasGraphicsQueue(VkPhysicalDevice device, std::uint32_t* family) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
    for (std::uint32_t i = 0; i < count; ++i) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            *family = i;
            return true;
        }
    }
    return false;
}

}  // namespace

std::string vulkanResultName(VkResult result) {
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
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: {
            std::ostringstream out;
            out << "VkResult(" << static_cast<int>(result) << ")";
            return out.str();
        }
    }
}

bool initGpuContext(GpuContext& context, std::string* error) {
    if (std::getenv("VK_ICD_FILENAMES") == nullptr) {
        const char* lvp = "/usr/share/vulkan/icd.d/lvp_icd.json";
        if (FILE* f = std::fopen(lvp, "rb")) {
            std::fclose(f);
            setenv("VK_ICD_FILENAMES", lvp, 0);
        }
    }
    const char* icd = std::getenv("VK_ICD_FILENAMES");
    context.icdPath = icd ? icd : "";

    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (!glfwInit()) {
        if (error) *error = "glfwInit failed on the null platform";
        return false;
    }
    context.glfwVersion = glfwGetVersionString();
    context.glfwPlatform = static_cast<int>(glfwGetPlatform());

    const bool wantValidation = std::getenv("SSOR_VALIDATION") != nullptr;
    const bool validationAvailable = layerAvailable("VK_LAYER_KHRONOS_validation");
    std::vector<const char*> layers;
    if (wantValidation && validationAvailable) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        context.validationEnabled = true;
    }

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "ssor_reconstruct";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "ssor";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();
    if (!vkOk(vkCreateInstance(&instanceInfo, nullptr, &context.instance),
              "vkCreateInstance", error)) {
        return false;
    }

    std::uint32_t deviceCount = 0;
    if (!vkOk(vkEnumeratePhysicalDevices(context.instance, &deviceCount, nullptr),
              "vkEnumeratePhysicalDevices", error)) {
        return false;
    }
    if (deviceCount == 0) {
        if (error) *error = "no Vulkan physical device found";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(context.instance, &deviceCount, devices.data());
    std::uint32_t selectedFamily = 0;
    for (VkPhysicalDevice device : devices) {
        if (hasGraphicsQueue(device, &selectedFamily)) {
            context.physicalDevice = device;
            context.queueFamily = selectedFamily;
            break;
        }
    }
    if (context.physicalDevice == VK_NULL_HANDLE) {
        if (error) *error = "no Vulkan device with a graphics queue";
        return false;
    }
    vkGetPhysicalDeviceProperties(context.physicalDevice, &context.properties);
    vkGetPhysicalDeviceMemoryProperties(context.physicalDevice,
                                        &context.memoryProperties);
    context.timestampPeriod = context.properties.limits.timestampPeriod;
    context.timestampValid =
        context.properties.limits.timestampComputeAndGraphics != 0 &&
        context.timestampPeriod > 0.0f;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = context.queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    if (!vkOk(vkCreateDevice(context.physicalDevice, &deviceInfo, nullptr,
                             &context.device),
              "vkCreateDevice", error)) {
        return false;
    }
    vkGetDeviceQueue(context.device, context.queueFamily, 0, &context.queue);

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context.queueFamily;
    if (!vkOk(vkCreateCommandPool(context.device, &poolInfo, nullptr,
                                  &context.commandPool),
              "vkCreateCommandPool", error)) {
        return false;
    }
    return true;
}

void shutdownGpuContext(GpuContext& context) {
    if (context.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context.device);
        if (context.commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(context.device, context.commandPool, nullptr);
        }
        vkDestroyDevice(context.device, nullptr);
    }
    if (context.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(context.instance, nullptr);
    }
    if (!context.glfwVersion.empty()) glfwTerminate();
    context = GpuContext{};
}

std::uint32_t findMemoryType(const GpuContext& context, std::uint32_t typeBits,
                             VkMemoryPropertyFlags properties) {
    for (std::uint32_t i = 0; i < context.memoryProperties.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) != 0 &&
            (context.memoryProperties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
            return i;
        }
    }
    return 0xffffffffu;
}

bool createBuffer(const GpuContext& context, VkDeviceSize size,
                  VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                  GpuBuffer& buffer, std::string* error) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!vkOk(vkCreateBuffer(context.device, &info, nullptr, &buffer.buffer),
              "vkCreateBuffer", error)) {
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context.device, buffer.buffer, &requirements);
    const std::uint32_t memoryType =
        findMemoryType(context, requirements.memoryTypeBits, properties);
    if (memoryType == 0xffffffffu) {
        if (error) *error = "no suitable memory type for buffer";
        return false;
    }
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType;
    if (!vkOk(vkAllocateMemory(context.device, &allocate, nullptr,
                               &buffer.memory),
              "vkAllocateMemory(buffer)", error)) {
        return false;
    }
    vkBindBufferMemory(context.device, buffer.buffer, buffer.memory, 0);
    buffer.size = size;
    buffer.hostVisible =
        (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    if (buffer.hostVisible) {
        if (!vkOk(vkMapMemory(context.device, buffer.memory, 0, size, 0,
                              &buffer.mapped),
                  "vkMapMemory", error)) {
            return false;
        }
    }
    return true;
}

void destroyBuffer(const GpuContext& context, GpuBuffer& buffer) {
    if (buffer.mapped) vkUnmapMemory(context.device, buffer.memory);
    if (buffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(context.device, buffer.buffer, nullptr);
    }
    if (buffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(context.device, buffer.memory, nullptr);
    }
    buffer = GpuBuffer{};
}

std::uint32_t bytesPerPixel(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UNORM: return 1;
        case VK_FORMAT_R8G8B8A8_UNORM: return 4;
        case VK_FORMAT_R32_UINT: return 4;
        case VK_FORMAT_R32_SFLOAT: return 4;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
        case VK_FORMAT_D32_SFLOAT: return 4;
        default: return 0;
    }
}

bool createImage(const GpuContext& context, std::uint32_t width,
                 std::uint32_t height, VkFormat format,
                 VkImageUsageFlags usage, GpuImage& image, std::string* error) {
    image.format = format;
    image.width = width;
    image.height = height;
    image.bytesPerPixel = bytesPerPixel(format);
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!vkOk(vkCreateImage(context.device, &info, nullptr, &image.image),
              "vkCreateImage", error)) {
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(context.device, image.image, &requirements);
    const std::uint32_t memoryType = findMemoryType(
        context, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == 0xffffffffu) {
        if (error) *error = "no device-local memory type for image";
        return false;
    }
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType;
    if (!vkOk(vkAllocateMemory(context.device, &allocate, nullptr,
                               &image.memory),
              "vkAllocateMemory(image)", error)) {
        return false;
    }
    vkBindImageMemory(context.device, image.image, image.memory, 0);
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (format == VK_FORMAT_D32_SFLOAT) {
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (!vkOk(vkCreateImageView(context.device, &viewInfo, nullptr, &image.view),
              "vkCreateImageView", error)) {
        return false;
    }
    return true;
}

void destroyImage(const GpuContext& context, GpuImage& image) {
    if (image.view != VK_NULL_HANDLE) {
        vkDestroyImageView(context.device, image.view, nullptr);
    }
    if (image.image != VK_NULL_HANDLE) {
        vkDestroyImage(context.device, image.image, nullptr);
    }
    if (image.memory != VK_NULL_HANDLE) {
        vkFreeMemory(context.device, image.memory, nullptr);
    }
    image = GpuImage{};
}

VkShaderModule createShaderModule(const GpuContext& context,
                                  const std::uint32_t* words,
                                  std::size_t byteSize,
                                  std::string* error) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = byteSize;
    info.pCode = words;
    VkShaderModule module = VK_NULL_HANDLE;
    if (!vkOk(vkCreateShaderModule(context.device, &info, nullptr, &module),
              "vkCreateShaderModule", error)) {
        return VK_NULL_HANDLE;
    }
    return module;
}

VkCommandBuffer beginOneShot(const GpuContext& context, std::string* error) {
    VkCommandBufferAllocateInfo allocate{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = context.commandPool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (!vkOk(vkAllocateCommandBuffers(context.device, &allocate, &commandBuffer),
              "vkAllocateCommandBuffers", error)) {
        return VK_NULL_HANDLE;
    }
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!vkOk(vkBeginCommandBuffer(commandBuffer, &begin),
              "vkBeginCommandBuffer", error)) {
        return VK_NULL_HANDLE;
    }
    return commandBuffer;
}

bool endOneShot(const GpuContext& context, VkCommandBuffer commandBuffer,
                std::string* error) {
    if (!vkOk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer", error)) {
        return false;
    }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    if (!vkOk(vkQueueSubmit(context.queue, 1, &submit, VK_NULL_HANDLE),
              "vkQueueSubmit", error)) {
        return false;
    }
    if (!vkOk(vkQueueWaitIdle(context.queue), "vkQueueWaitIdle", error)) {
        return false;
    }
    vkFreeCommandBuffers(context.device, context.commandPool, 1, &commandBuffer);
    return true;
}

bool waitIdle(const GpuContext& context, std::string* error) {
    return vkOk(vkDeviceWaitIdle(context.device), "vkDeviceWaitIdle", error);
}

std::string vulkanFormatName(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UNORM: return "VK_FORMAT_R8_UNORM";
        case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
        case VK_FORMAT_R32_UINT: return "VK_FORMAT_R32_UINT";
        case VK_FORMAT_R32_SFLOAT: return "VK_FORMAT_R32_SFLOAT";
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return "VK_FORMAT_R32G32B32A32_SFLOAT";
        case VK_FORMAT_D32_SFLOAT: return "VK_FORMAT_D32_SFLOAT";
        default: return "VK_FORMAT(" + std::to_string(static_cast<int>(format)) + ")";
    }
}

std::string vulkanLayoutName(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return "COLOR_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return "DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return "SHADER_READ_ONLY_OPTIMAL";
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return "TRANSFER_SRC_OPTIMAL";
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return "TRANSFER_DST_OPTIMAL";
        case VK_IMAGE_LAYOUT_GENERAL: return "GENERAL";
        default: return "LAYOUT(" + std::to_string(static_cast<int>(layout)) + ")";
    }
}

std::string vulkanStageName(VkPipelineStageFlags stage) {
    switch (stage) {
        case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT: return "TOP_OF_PIPE";
        case VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:
            return "COLOR_ATTACHMENT_OUTPUT";
        case VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT:
            return "EARLY_FRAGMENT_TESTS";
        case VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT: return "FRAGMENT_SHADER";
        case VK_PIPELINE_STAGE_TRANSFER_BIT: return "TRANSFER";
        case VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT: return "BOTTOM_OF_PIPE";
        default: return "STAGE(0x" + std::to_string(stage) + ")";
    }
}

std::string vulkanAccessName(VkAccessFlags access) {
    switch (access) {
        case 0: return "NONE";
        case VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:
            return "COLOR_ATTACHMENT_WRITE";
        case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
            return "DEPTH_STENCIL_ATTACHMENT_WRITE";
        case VK_ACCESS_SHADER_READ_BIT: return "SHADER_READ";
        case VK_ACCESS_TRANSFER_READ_BIT: return "TRANSFER_READ";
        case VK_ACCESS_TRANSFER_WRITE_BIT: return "TRANSFER_WRITE";
        case VK_ACCESS_MEMORY_READ_BIT: return "MEMORY_READ";
        case VK_ACCESS_MEMORY_WRITE_BIT: return "MEMORY_WRITE";
        default: return "ACCESS(0x" + std::to_string(access) + ")";
    }
}

}  // namespace ssor
