#include "vulkan_renderer.hpp"

#include "checksum.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef RR_SHADER_DIR
#define RR_SHADER_DIR "shaders"
#endif

namespace rr {
namespace {

constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

std::string vkResultName(VkResult result) {
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
        case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        default: return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
    }
}

bool vkOk(VkResult result, std::string& error, const char* operation) {
    if (result == VK_SUCCESS) {
        return true;
    }
    error = std::string(operation) + " failed: " + vkResultName(result);
    return false;
}

uint32_t findMemoryType(VkPhysicalDevice physical,
                        uint32_t typeBits,
                        VkMemoryPropertyFlags required,
                        bool& found) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((typeBits & (1U << i)) != 0U &&
            (properties.memoryTypes[i].propertyFlags & required) == required) {
            found = true;
            return i;
        }
    }
    found = false;
    return 0;
}

std::vector<std::uint32_t> loadSpirv(const std::filesystem::path& path, std::string& error) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = "cannot open SPIR-V shader: " + path.string();
        return {};
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0 || (size % 4) != 0) {
        error = "invalid SPIR-V size: " + path.string();
        return {};
    }
    stream.seekg(0);
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4U);
    stream.read(reinterpret_cast<char*>(words.data()), size);
    if (!stream || words.empty() || words.front() != 0x07230203U) {
        error = "invalid SPIR-V magic: " + path.string();
        return {};
    }
    return words;
}

const char* deviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated_gpu";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete_gpu";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual_gpu";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
        default: return "other";
    }
}

std::string versionString(std::uint32_t version) {
    std::ostringstream stream;
    stream << VK_VERSION_MAJOR(version) << '.' << VK_VERSION_MINOR(version) << '.'
           << VK_VERSION_PATCH(version);
    return stream.str();
}

bool hasLayer(const char* name) {
    uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) {
        return false;
    }
    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, name) == 0) {
            return true;
        }
    }
    return false;
}

std::array<float, 4> clearColor(const std::array<std::uint8_t, 4>& rgba) {
    return {static_cast<float>(rgba[0]) / 255.0F,
            static_cast<float>(rgba[1]) / 255.0F,
            static_cast<float>(rgba[2]) / 255.0F,
            static_cast<float>(rgba[3]) / 255.0F};
}

}  // namespace

struct VulkanRenderer::Impl {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    DeviceInfo info;
    bool validationEnabled = false;
    std::vector<std::uint32_t> vertexSpirv;
    std::vector<std::uint32_t> fragmentSpirv;

    bool createInstance(const RenderOptions& options, std::string& error) {
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "raster-regression";
        application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        application.pEngineName = "rr";
        application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        application.apiVersion = VK_API_VERSION_1_1;

        std::vector<const char*> layers;
        if (options.enableValidation && hasLayer("VK_LAYER_KHRONOS_validation")) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            validationEnabled = true;
        }
        std::vector<const char*> extensions;
        if (validationEnabled) {
            uint32_t count = 0;
            if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) == VK_SUCCESS) {
                std::vector<VkExtensionProperties> available(count);
                if (vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data()) ==
                    VK_SUCCESS) {
                    for (const auto& extension : available) {
                        if (std::strcmp(extension.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) ==
                            0) {
                            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                            break;
                        }
                    }
                }
            }
        }

        VkInstanceCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create.pApplicationInfo = &application;
        create.enabledLayerCount = static_cast<uint32_t>(layers.size());
        create.ppEnabledLayerNames = layers.data();
        create.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        create.ppEnabledExtensionNames = extensions.data();
        return vkOk(vkCreateInstance(&create, nullptr, &instance), error,
                    "vkCreateInstance");
    }

    bool selectPhysicalDevice(const RenderOptions& options, std::string& error) {
        uint32_t count = 0;
        if (!vkOk(vkEnumeratePhysicalDevices(instance, &count, nullptr), error,
                  "vkEnumeratePhysicalDevices")) {
            return false;
        }
        if (count == 0) {
            error = "no Vulkan physical devices were found";
            return false;
        }
        std::vector<VkPhysicalDevice> devices(count);
        if (!vkOk(vkEnumeratePhysicalDevices(instance, &count, devices.data()), error,
                  "vkEnumeratePhysicalDevices")) {
            return false;
        }

        int selected = -1;
        if (options.preferredDeviceIndex >= 0) {
            if (static_cast<std::size_t>(options.preferredDeviceIndex) >= devices.size()) {
                error = "preferred device index is out of range";
                return false;
            }
            selected = options.preferredDeviceIndex;
        } else if (!options.preferredDeviceName.empty()) {
            for (uint32_t i = 0; i < count; ++i) {
                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(devices[i], &properties);
                if (std::string(properties.deviceName).find(options.preferredDeviceName) !=
                    std::string::npos) {
                    selected = static_cast<int>(i);
                    break;
                }
            }
            if (selected < 0) {
                error = "no Vulkan device name matched: " + options.preferredDeviceName;
                return false;
            }
        } else {
            selected = 0;
        }

        for (uint32_t i = 0; i < count; ++i) {
            VkPhysicalDevice candidate = devices[i];
            uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
            bool hasGraphics = false;
            for (const auto& family : families) {
                if ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
                    hasGraphics = true;
                    break;
                }
            }
            if (selected >= 0 && static_cast<int>(i) != selected) {
                continue;
            }
            if (hasGraphics) {
                selected = static_cast<int>(i);
                break;
            }
        }
        if (selected < 0) {
            error = "no Vulkan device exposes a graphics queue";
            return false;
        }
        physical = devices[static_cast<std::size_t>(selected)];

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
        bool foundQueue = false;
        for (uint32_t i = 0; i < familyCount; ++i) {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
                queueFamily = i;
                foundQueue = true;
                break;
            }
        }
        if (!foundQueue) {
            error = "selected Vulkan device lost its graphics queue";
            return false;
        }
        info.name = properties.deviceName;
        info.apiVersion = properties.apiVersion;
        info.driverVersion = properties.driverVersion;
        info.vendorId = properties.vendorID;
        info.deviceId = properties.deviceID;
        info.deviceType = deviceTypeName(properties.deviceType);
        info.queueFamily = queueFamily;
        info.maxFramebufferWidth = properties.limits.maxFramebufferWidth;
        info.maxFramebufferHeight = properties.limits.maxFramebufferHeight;
        info.lineWidthRange[0] = properties.limits.lineWidthRange[0];
        info.lineWidthRange[1] = properties.limits.lineWidthRange[1];
        if (properties.apiVersion >= VK_API_VERSION_1_2) {
            VkPhysicalDeviceDriverProperties driver{};
            driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
            VkPhysicalDeviceProperties2 properties2{};
            properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties2.pNext = &driver;
            vkGetPhysicalDeviceProperties2(physical, &properties2);
            info.driverName = driver.driverName;
            info.driverInfo = driver.driverInfo;
        }
        return true;
    }

    bool createDevice(std::string& error) {
        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(physical, &supported);
        info.wideLines = supported.wideLines == VK_TRUE;
        info.fillModeNonSolid = supported.fillModeNonSolid == VK_TRUE;
        info.depthClamp = supported.depthClamp == VK_TRUE;

        VkPhysicalDeviceFeatures enabled{};
        enabled.wideLines = supported.wideLines;
        enabled.fillModeNonSolid = supported.fillModeNonSolid;
        enabled.depthClamp = supported.depthClamp;

        const float priority = 1.0F;
        VkDeviceQueueCreateInfo queueCreate{};
        queueCreate.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreate.queueFamilyIndex = queueFamily;
        queueCreate.queueCount = 1;
        queueCreate.pQueuePriorities = &priority;

        VkDeviceCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create.queueCreateInfoCount = 1;
        create.pQueueCreateInfos = &queueCreate;
        create.pEnabledFeatures = &enabled;
        if (!vkOk(vkCreateDevice(physical, &create, nullptr, &device), error,
                  "vkCreateDevice")) {
            return false;
        }
        vkGetDeviceQueue(device, queueFamily, 0, &queue);

        VkCommandPoolCreateInfo poolCreate{};
        poolCreate.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCreate.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolCreate.queueFamilyIndex = queueFamily;
        if (!vkOk(vkCreateCommandPool(device, &poolCreate, nullptr, &commandPool), error,
                  "vkCreateCommandPool")) {
            return false;
        }
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = commandPool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        if (!vkOk(vkAllocateCommandBuffers(device, &allocate, &commandBuffer), error,
                  "vkAllocateCommandBuffers")) {
            return false;
        }
        VkFenceCreateInfo fenceCreate{};
        fenceCreate.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        return vkOk(vkCreateFence(device, &fenceCreate, nullptr, &fence), error,
                    "vkCreateFence");
    }
};

namespace {

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    bool hostVisible = false;
    void* mapped = nullptr;
};

struct ColorTarget {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkExtent2D extent{};
};

void destroyBuffer(VkDevice device, Buffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer.buffer, nullptr);
    }
    if (buffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffer.memory, nullptr);
    }
    buffer = {};
}

void destroyTarget(VkDevice device, ColorTarget& target) {
    if (target.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, target.view, nullptr);
    }
    if (target.image != VK_NULL_HANDLE) {
        vkDestroyImage(device, target.image, nullptr);
    }
    if (target.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, target.memory, nullptr);
    }
    target = {};
}

bool createBuffer(VulkanRenderer::Impl& impl,
                  VkDeviceSize size,
                  VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags propertyFlags,
                  Buffer& buffer,
                  std::string& error) {
    VkBufferCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create.size = std::max<VkDeviceSize>(size, 4U);
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!vkOk(vkCreateBuffer(impl.device, &create, nullptr, &buffer.buffer), error,
              "vkCreateBuffer")) {
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(impl.device, buffer.buffer, &requirements);
    bool found = false;
    const uint32_t type = findMemoryType(
        impl.physical, requirements.memoryTypeBits, propertyFlags, found);
    if (!found) {
        error = "no compatible buffer memory type";
        return false;
    }
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    if (!vkOk(vkAllocateMemory(impl.device, &allocate, nullptr, &buffer.memory), error,
              "vkAllocateMemory")) {
        return false;
    }
    if (!vkOk(vkBindBufferMemory(impl.device, buffer.buffer, buffer.memory, 0), error,
              "vkBindBufferMemory")) {
        return false;
    }
    buffer.size = create.size;
    buffer.hostVisible = (propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U;
    return true;
}

bool beginCommands(VulkanRenderer::Impl& impl, std::string& error) {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    return vkOk(vkResetCommandBuffer(impl.commandBuffer, 0), error,
                "vkResetCommandBuffer") &&
           vkOk(vkBeginCommandBuffer(impl.commandBuffer, &begin), error,
                 "vkBeginCommandBuffer");
}

bool endSubmitWait(VulkanRenderer::Impl& impl, std::string& error) {
    if (!vkOk(vkEndCommandBuffer(impl.commandBuffer), error, "vkEndCommandBuffer")) {
        return false;
    }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &impl.commandBuffer;
    if (!vkOk(vkResetFences(impl.device, 1, &impl.fence), error, "vkResetFences") ||
        !vkOk(vkQueueSubmit(impl.queue, 1, &submit, impl.fence), error, "vkQueueSubmit") ||
        !vkOk(vkWaitForFences(impl.device, 1, &impl.fence, VK_TRUE,
                              std::numeric_limits<std::uint64_t>::max()),
              error, "vkWaitForFences")) {
        return false;
    }
    return true;
}

bool uploadBuffer(VulkanRenderer::Impl& impl,
                  Buffer& destination,
                  const void* data,
                  VkDeviceSize size,
                  std::string& error) {
    if (size == 0U) {
        return true;
    }
    if (destination.hostVisible) {
        void* mapped = nullptr;
        if (!vkOk(vkMapMemory(impl.device, destination.memory, 0, size, 0, &mapped), error,
                  "vkMapMemory")) {
            return false;
        }
        std::memcpy(mapped, data, static_cast<std::size_t>(size));
        vkUnmapMemory(impl.device, destination.memory);
        return true;
    }

    Buffer staging;
    if (!createBuffer(impl, size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging, error)) {
        return false;
    }
    void* mapped = nullptr;
    if (!vkOk(vkMapMemory(impl.device, staging.memory, 0, size, 0, &mapped), error,
              "vkMapMemory(staging)")) {
        destroyBuffer(impl.device, staging);
        return false;
    }
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    vkUnmapMemory(impl.device, staging.memory);
    if (!beginCommands(impl, error)) {
        destroyBuffer(impl.device, staging);
        return false;
    }
    VkBufferCopy copy{};
    copy.size = size;
    vkCmdCopyBuffer(impl.commandBuffer, staging.buffer, destination.buffer, 1, &copy);
    const bool ok = endSubmitWait(impl, error);
    destroyBuffer(impl.device, staging);
    return ok;
}

bool createColorTarget(VulkanRenderer::Impl& impl,
                       uint32_t width,
                       uint32_t height,
                       ColorTarget& target,
                       std::string& error) {
    target.extent = {width, height};
    VkImageCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = kColorFormat;
    create.extent = {width, height, 1};
    create.mipLevels = 1;
    create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!vkOk(vkCreateImage(impl.device, &create, nullptr, &target.image), error,
              "vkCreateImage")) {
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(impl.device, target.image, &requirements);
    bool found = false;
    const uint32_t type =
        findMemoryType(impl.physical, requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, found);
    if (!found) {
        error = "no compatible image memory type";
        return false;
    }
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    if (!vkOk(vkAllocateMemory(impl.device, &allocate, nullptr, &target.memory), error,
              "vkAllocateMemory(image)") ||
        !vkOk(vkBindImageMemory(impl.device, target.image, target.memory, 0), error,
              "vkBindImageMemory")) {
        return false;
    }
    VkImageViewCreateInfo viewCreate{};
    viewCreate.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCreate.image = target.image;
    viewCreate.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCreate.format = kColorFormat;
    viewCreate.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCreate.subresourceRange.levelCount = 1;
    viewCreate.subresourceRange.layerCount = 1;
    return vkOk(vkCreateImageView(impl.device, &viewCreate, nullptr, &target.view), error,
                "vkCreateImageView");
}

void imageBarrier(VkCommandBuffer commandBuffer,
                  VkImage image,
                  VkImageLayout oldLayout,
                  VkImageLayout newLayout,
                  VkAccessFlags sourceAccess,
                  VkAccessFlags destinationAccess,
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
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
}

bool copyTargetToBuffer(VulkanRenderer::Impl& impl,
                        const ColorTarget& target,
                        Buffer& readback,
                        Image& out,
                        std::string& error) {
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(target.extent.width) * target.extent.height * 4U;
    if (readback.buffer == VK_NULL_HANDLE || readback.size < bytes) {
        destroyBuffer(impl.device, readback);
        if (!createBuffer(impl, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          readback, error)) {
            return false;
        }
    }
    if (!beginCommands(impl, error)) {
        return false;
    }
    imageBarrier(impl.commandBuffer, target.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {target.extent.width, target.extent.height, 1};
    vkCmdCopyImageToBuffer(impl.commandBuffer, target.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
    imageBarrier(impl.commandBuffer, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if (!endSubmitWait(impl, error)) {
        return false;
    }
    void* mapped = nullptr;
    if (!vkOk(vkMapMemory(impl.device, readback.memory, 0, bytes, 0, &mapped), error,
              "vkMapMemory(readback)")) {
        return false;
    }
    out.width = target.extent.width;
    out.height = target.extent.height;
    out.rgba.assign(static_cast<const std::uint8_t*>(mapped),
                    static_cast<const std::uint8_t*>(mapped) + bytes);
    vkUnmapMemory(impl.device, readback.memory);
    return true;
}

bool clearTarget(VulkanRenderer::Impl& impl,
                 const ColorTarget& target,
                 const std::array<std::uint8_t, 4>& color,
                 std::string& error) {
    if (!beginCommands(impl, error)) {
        return false;
    }
    imageBarrier(impl.commandBuffer, target.image, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    const auto clear = clearColor(color);
    VkClearColorValue value{};
    value.float32[0] = clear[0];
    value.float32[1] = clear[1];
    value.float32[2] = clear[2];
    value.float32[3] = clear[3];
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearColorImage(impl.commandBuffer, target.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
    imageBarrier(impl.commandBuffer, target.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    return endSubmitWait(impl, error);
}

VkShaderModule createShaderModule(VkDevice device,
                                  const std::vector<std::uint32_t>& words,
                                  std::string& error) {
    VkShaderModuleCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create.codeSize = words.size() * sizeof(std::uint32_t);
    create.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (!vkOk(vkCreateShaderModule(device, &create, nullptr, &module), error,
              "vkCreateShaderModule")) {
        return VK_NULL_HANDLE;
    }
    return module;
}

VkCullModeFlags cullMode(const std::string& mode) {
    if (mode == "front") return VK_CULL_MODE_FRONT_BIT;
    if (mode == "back") return VK_CULL_MODE_BACK_BIT;
    return VK_CULL_MODE_NONE;
}

VkFrontFace frontFace(const std::string& mode) {
    return mode == "cw" ? VK_FRONT_FACE_CLOCKWISE
                        : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

struct PipelineResult {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    Rule effectiveRule;
    std::vector<std::string> warnings;
};

PipelineResult createPipeline(VulkanRenderer::Impl& impl,
                              const Rule& requested,
                              uint32_t width,
                              uint32_t height,
                              VkRenderPass renderPass,
                              std::string& error) {
    PipelineResult result;
    result.effectiveRule = requested;
    if ((requested.polygon_mode == "line" || requested.polygon_mode == "point") &&
        !impl.info.fillModeNonSolid) {
        result.warnings.push_back(
            "fillModeNonSolid unsupported; polygon mode fell back to fill");
        result.effectiveRule.polygon_mode = "fill";
    }
    if (requested.line_width > 1.0 && !impl.info.wideLines) {
        result.warnings.push_back("wideLines unsupported; line width clamped to 1.0");
        result.effectiveRule.line_width = 1.0;
    }
    if (requested.line_width <
            static_cast<double>(impl.info.lineWidthRange[0]) ||
        requested.line_width >
            static_cast<double>(impl.info.lineWidthRange[1])) {
        std::ostringstream warning;
        warning << "line width " << requested.line_width << " clamped to device range ["
                << impl.info.lineWidthRange[0] << ", "
                << impl.info.lineWidthRange[1] << "]";
        result.warnings.push_back(warning.str());
    }
    result.effectiveRule.line_width =
        std::clamp(result.effectiveRule.line_width,
                   static_cast<double>(impl.info.lineWidthRange[0]),
                   static_cast<double>(impl.info.lineWidthRange[1]));
    if (requested.depth_clamp && !impl.info.depthClamp) {
        result.warnings.push_back("depthClamp unsupported; feature disabled");
        result.effectiveRule.depth_clamp = false;
    }

    const double maxSpan =
        static_cast<double>(std::max(width, height)) * 4.0 + 1.0;
    auto bounded = [&result, &requested](const char* name,
                                         double value,
                                         double minimum,
                                         double maximum) {
        const double clamped = std::clamp(value, minimum, maximum);
        if (clamped != value) {
            std::ostringstream warning;
            warning << name << ' ' << value << " clamped to " << clamped;
            result.warnings.push_back(warning.str());
        }
        return clamped;
    };
    VkViewport viewport{};
    viewport.x = static_cast<float>(
        bounded("viewport_x", requested.viewport_x, -maxSpan, maxSpan));
    viewport.y = static_cast<float>(
        bounded("viewport_y", requested.viewport_y, -maxSpan, maxSpan));
    viewport.width = static_cast<float>(bounded(
        "viewport_width",
        requested.viewport_width < 0.0 ? static_cast<double>(width)
                                       : requested.viewport_width,
        1.0, maxSpan));
    viewport.height = static_cast<float>(bounded(
        "viewport_height",
        requested.viewport_height < 0.0 ? static_cast<double>(height)
                                        : requested.viewport_height,
        1.0, maxSpan));
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    result.effectiveRule.viewport_width = viewport.width;
    result.effectiveRule.viewport_height = viewport.height;

    VkRect2D scissor{};
    const double requestedScissorX =
        requested.scissor_x < 0.0 ? 0.0 : requested.scissor_x;
    const double requestedScissorY =
        requested.scissor_y < 0.0 ? 0.0 : requested.scissor_y;
    const double scissorWidth =
        requested.scissor_width < 0.0 ? static_cast<double>(width)
                                      : requested.scissor_width;
    const double scissorHeight =
        requested.scissor_height < 0.0 ? static_cast<double>(height)
                                       : requested.scissor_height;
    scissor.offset.x = static_cast<int32_t>(bounded(
        "scissor_x", requestedScissorX, 0.0, static_cast<double>(width - 1U)));
    scissor.offset.y = static_cast<int32_t>(bounded(
        "scissor_y", requestedScissorY, 0.0, static_cast<double>(height - 1U)));
    const auto maxScissorWidth =
        width > static_cast<uint32_t>(scissor.offset.x)
            ? width - static_cast<uint32_t>(scissor.offset.x)
            : 1U;
    const auto maxScissorHeight =
        height > static_cast<uint32_t>(scissor.offset.y)
            ? height - static_cast<uint32_t>(scissor.offset.y)
            : 1U;
    scissor.extent.width = static_cast<uint32_t>(bounded(
        "scissor_width", scissorWidth, 1.0,
        static_cast<double>(maxScissorWidth)));
    scissor.extent.height = static_cast<uint32_t>(bounded(
        "scissor_height", scissorHeight, 1.0,
        static_cast<double>(maxScissorHeight)));
    result.effectiveRule.scissor_x = scissor.offset.x;
    result.effectiveRule.scissor_y = scissor.offset.y;
    result.effectiveRule.scissor_width = scissor.extent.width;
    result.effectiveRule.scissor_height = scissor.extent.height;

    VkShaderModule vertexModule =
        createShaderModule(impl.device, impl.vertexSpirv, error);
    if (vertexModule == VK_NULL_HANDLE) {
        return result;
    }
    VkShaderModule fragmentModule =
        createShaderModule(impl.device, impl.fragmentSpirv, error);
    if (fragmentModule == VK_NULL_HANDLE) {
        vkDestroyShaderModule(impl.device, vertexModule, nullptr);
        return result;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attributes[2]{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[0].offset = offsetof(Vertex, x);
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attributes[1].offset = offsetof(Vertex, rgba);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.depthClampEnable = result.effectiveRule.depth_clamp ? VK_TRUE : VK_FALSE;
    rasterization.rasterizerDiscardEnable =
        result.effectiveRule.rasterizer_discard ? VK_TRUE : VK_FALSE;
    rasterization.polygonMode =
        result.effectiveRule.polygon_mode == "line"
            ? VK_POLYGON_MODE_LINE
            : (result.effectiveRule.polygon_mode == "point" ? VK_POLYGON_MODE_POINT
                                                           : VK_POLYGON_MODE_FILL);
    rasterization.cullMode = cullMode(result.effectiveRule.cull_mode);
    rasterization.frontFace = frontFace(result.effectiveRule.front_face);
    rasterization.depthBiasEnable =
        (requested.depth_bias_constant != 0.0 || requested.depth_bias_slope != 0.0 ||
         requested.depth_bias_clamp != 0.0)
            ? VK_TRUE
            : VK_FALSE;
    rasterization.depthBiasConstantFactor =
        static_cast<float>(requested.depth_bias_constant);
    rasterization.depthBiasSlopeFactor =
        static_cast<float>(requested.depth_bias_slope);
    rasterization.depthBiasClamp =
        static_cast<float>(requested.depth_bias_clamp);
    rasterization.lineWidth = static_cast<float>(result.effectiveRule.line_width);

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                     VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT |
                                     VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = requested.additive_blend ? VK_TRUE : VK_FALSE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor =
        requested.additive_blend ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor =
        requested.additive_blend ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkPipelineLayoutCreateInfo layoutCreate{};
    layoutCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (!vkOk(vkCreatePipelineLayout(impl.device, &layoutCreate, nullptr, &result.layout),
              error, "vkCreatePipelineLayout")) {
        vkDestroyShaderModule(impl.device, fragmentModule, nullptr);
        vkDestroyShaderModule(impl.device, vertexModule, nullptr);
        return result;
    }

    VkGraphicsPipelineCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create.stageCount = 2;
    create.pStages = stages;
    create.pVertexInputState = &vertexInput;
    create.pInputAssemblyState = &assembly;
    create.pViewportState = &viewportState;
    create.pRasterizationState = &rasterization;
    create.pMultisampleState = &multisample;
    create.pColorBlendState = &blend;
    create.layout = result.layout;
    create.renderPass = renderPass;
    create.subpass = 0;
    const VkResult status =
        vkCreateGraphicsPipelines(impl.device, VK_NULL_HANDLE, 1, &create, nullptr,
                                  &result.pipeline);
    vkDestroyShaderModule(impl.device, fragmentModule, nullptr);
    vkDestroyShaderModule(impl.device, vertexModule, nullptr);
    if (status != VK_SUCCESS) {
        error = "vkCreateGraphicsPipelines failed: " + vkResultName(status);
        return result;
    }
    return result;
}

}  // namespace

#include "vulkan_renderer_methods.inc"

}  // namespace


