#include "vulkan_engine.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>

#include "sha256.hpp"
#include "util.hpp"

namespace inspect {
namespace {

constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;

struct HostBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    bool coherent = true;
};

struct DeviceBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
};

std::string apiVersionString(std::uint32_t version) {
    std::ostringstream out;
    out << VK_VERSION_MAJOR(version) << '.' << VK_VERSION_MINOR(version) << '.'
        << VK_VERSION_PATCH(version);
    return out.str();
}

bool isValidMode(const std::string& mode) {
    return mode == "auto" || mode == "full" || mode == "compact" ||
           mode == "cpu";
}

std::uint64_t planRequiredBytes(const Material& material, std::uint32_t width,
                                std::uint32_t height) {
    const std::uint64_t input =
        static_cast<std::uint64_t>(material.vertices.size()) * kInputVertexStride +
        material.byte_size;
    const std::uint64_t capture =
        static_cast<std::uint64_t>(material.vertices.size()) * kCaptureRecordBytes;
    const std::uint64_t image =
        static_cast<std::uint64_t>(width) * height * 4ULL;
    return input + capture + image * 2ULL + 1ULL * kMiB;
}

std::string resultString(VkResult result) {
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
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        default:
            return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
    }
}

std::uint32_t findMemoryType(const VkPhysicalDeviceMemoryProperties& memory,
                             std::uint32_t type_bits,
                             VkMemoryPropertyFlags required) {
    for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
        if ((type_bits & (1U << index)) != 0U &&
            (memory.memoryTypes[index].propertyFlags & required) == required) {
            return index;
        }
    }
    return UINT32_MAX;
}

}  // namespace

struct VulkanEngine::Impl {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceMemoryProperties memory_properties{};
    VkPhysicalDeviceLimits limits{};
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    HostBuffer vertex_buffer;
    DeviceBuffer capture_buffer;
    HostBuffer staging_buffer;
    HostBuffer readback_buffer;
    VkPipeline capture_pipeline = VK_NULL_HANDLE;
    VkPipeline replay_pipeline = VK_NULL_HANDLE;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    bool createInstanceAndPhysical(std::string& error);
    bool createDevice(std::string& error);
    bool createResources(std::uint32_t width, std::uint32_t height,
                         const std::string& shader_dir, std::string& error);
    bool ensureHostBuffer(HostBuffer& buffer, VkDeviceSize size,
                          VkBufferUsageFlags usage, std::string& error);
    bool ensureDeviceBuffer(DeviceBuffer& buffer, VkDeviceSize size,
                            VkBufferUsageFlags usage, std::string& error);
    bool createImage(std::string& error);
    bool createRenderPass(std::string& error);
    bool createDescriptors(std::string& error);
    bool createPipelines(const std::string& shader_dir, std::string& error);
    bool createPipeline(const std::string& shader_dir, bool capture,
                        std::string& error);
    bool updateCaptureDescriptor(std::string& error);
    void destroyHostBuffer(HostBuffer& buffer);
    void destroyDeviceBuffer(DeviceBuffer& buffer);
    void destroyResources();
    void destroyDevice();
    void destroy();
};

std::string ResourcePlan::toJson() const {
    std::ostringstream out;
    out << "{\n";
    out << "  \"backend\": " << quoteJson(backend) << ",\n";
    out << "  \"requested_mode\": " << quoteJson(requested_mode) << ",\n";
    out << "  \"selected_mode\": " << quoteJson(selected_mode) << ",\n";
    out << "  \"device_name\": " << quoteJson(device_name) << ",\n";
    out << "  \"driver_name\": " << quoteJson(driver_name) << ",\n";
    out << "  \"api_version\": " << quoteJson(api_version) << ",\n";
    out << "  \"degraded\": " << (degraded ? "true" : "false") << ",\n";
    out << "  \"geometry_shader_supported\": "
        << (geometry_shader_supported ? "true" : "false") << ",\n";
    out << "  \"vertex_pipeline_stores_supported\": "
        << (vertex_pipeline_stores_supported ? "true" : "false") << ",\n";
    out << "  \"cpu_fallback_allowed\": "
        << (cpu_fallback_allowed ? "true" : "false") << ",\n";
    out << "  \"heap_bytes\": " << heap_bytes << ",\n";
    out << "  \"memory_budget_bytes\": " << memory_budget_bytes << ",\n";
    out << "  \"required_bytes\": " << required_bytes << ",\n";
    out << "  \"input_bytes\": " << input_bytes << ",\n";
    out << "  \"capture_bytes\": " << capture_bytes << ",\n";
    out << "  \"image_bytes\": " << image_bytes << ",\n";
    out << "  \"capture_records\": " << capture_records << ",\n";
    out << "  \"max_storage_buffer_range\": " << max_storage_buffer_range
        << ",\n";
    out << "  \"width\": " << width << ",\n";
    out << "  \"height\": " << height << ",\n";
    out << "  \"reasons\": [";
    for (std::size_t index = 0; index < reasons.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << quoteJson(reasons[index]);
    }
    out << "]\n";
    out << "}\n";
    return out.str();
}

ResourcePlan makeCpuFallbackPlan(const EngineOptions& options,
                                 const Material& material,
                                 const std::string& reason) {
    ResourcePlan plan;
    plan.backend = "cpu";
    plan.requested_mode = options.requested_mode;
    plan.selected_mode = "cpu";
    plan.degraded = true;
    plan.cpu_fallback_allowed = options.allow_cpu_fallback;
    plan.reasons.push_back(reason);
    plan.input_bytes =
        static_cast<std::uint64_t>(material.vertices.size()) * kInputVertexStride +
        material.byte_size;
    plan.capture_bytes =
        static_cast<std::uint64_t>(material.vertices.size()) * kCaptureRecordBytes;
    plan.capture_records = static_cast<std::uint32_t>(material.vertices.size());
    plan.width = options.requested_width == 0 ? 256 : options.requested_width;
    plan.height = options.requested_height == 0 ? 256 : options.requested_height;
    plan.image_bytes =
        static_cast<std::uint64_t>(plan.width) * plan.height * 4ULL;
    plan.required_bytes = plan.input_bytes + plan.capture_bytes +
                          plan.image_bytes * 2ULL;
    return plan;
}

VulkanEngine::VulkanEngine() = default;
VulkanEngine::~VulkanEngine() { shutdown(); }

void VulkanEngine::Impl::destroyHostBuffer(HostBuffer& buffer) {
    if (buffer.mapped != nullptr && buffer.memory != VK_NULL_HANDLE) {
        vkUnmapMemory(device, buffer.memory);
    }
    if (buffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer.buffer, nullptr);
    }
    if (buffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffer.memory, nullptr);
    }
    buffer = HostBuffer{};
}

void VulkanEngine::Impl::destroyDeviceBuffer(DeviceBuffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer.buffer, nullptr);
    }
    if (buffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffer.memory, nullptr);
    }
    buffer = DeviceBuffer{};
}

void VulkanEngine::Impl::destroyResources() {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    if (capture_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, capture_pipeline, nullptr);
        capture_pipeline = VK_NULL_HANDLE;
    }
    if (replay_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, replay_pipeline, nullptr);
        replay_pipeline = VK_NULL_HANDLE;
    }
    destroyHostBuffer(vertex_buffer);
    destroyDeviceBuffer(capture_buffer);
    destroyHostBuffer(staging_buffer);
    destroyHostBuffer(readback_buffer);
    if (framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;
    }
    if (image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, image_view, nullptr);
        image_view = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
    }
    if (image_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, image_memory, nullptr);
        image_memory = VK_NULL_HANDLE;
    }
    if (render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, render_pass, nullptr);
        render_pass = VK_NULL_HANDLE;
    }
    if (descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        descriptor_pool = VK_NULL_HANDLE;
    }
    if (pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        pipeline_layout = VK_NULL_HANDLE;
    }
    if (descriptor_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
        descriptor_layout = VK_NULL_HANDLE;
    }
}

void VulkanEngine::Impl::destroyDevice() {
    destroyResources();
    if (device != VK_NULL_HANDLE) {
        if (command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, command_pool, nullptr);
            command_pool = VK_NULL_HANDLE;
            command_buffer = VK_NULL_HANDLE;
        }
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
        queue = VK_NULL_HANDLE;
    }
}

void VulkanEngine::Impl::destroy() {
    destroyDevice();
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
    physical = VK_NULL_HANDLE;
}

bool VulkanEngine::Impl::createInstanceAndPhysical(std::string& error) {
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "gs_inspect";
    application.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    application.pEngineName = "geometry-shader-output-inspector";
    application.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    application.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &application;
    VkResult result = vkCreateInstance(&instance_info, nullptr, &instance);
    if (result != VK_SUCCESS) {
        error = "vkCreateInstance failed: " + resultString(result);
        return false;
    }

    std::uint32_t device_count = 0;
    result = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (result != VK_SUCCESS || device_count == 0) {
        error = "no Vulkan physical device available";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

    VkPhysicalDevice selected = VK_NULL_HANDLE;
    std::uint32_t selected_family = 0;
    for (VkPhysicalDevice candidate : devices) {
        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count,
                                                 nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count,
                                                 families.data());
        for (std::uint32_t index = 0; index < family_count; ++index) {
            if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
                selected = candidate;
                selected_family = index;
                break;
            }
        }
        if (selected != VK_NULL_HANDLE) {
            break;
        }
    }
    if (selected == VK_NULL_HANDLE) {
        error = "no Vulkan graphics queue found";
        return false;
    }

    physical = selected;
    queue_family = selected_family;
    vkGetPhysicalDeviceProperties(physical, &properties);
    vkGetPhysicalDeviceFeatures(physical, &features);
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    limits = properties.limits;
    return true;
}

bool VulkanEngine::Impl::createDevice(std::string& error) {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    VkPhysicalDeviceFeatures enabled_features{};
    enabled_features.geometryShader = VK_TRUE;
    enabled_features.vertexPipelineStoresAndAtomics = VK_TRUE;

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.pEnabledFeatures = &enabled_features;
    VkResult result = vkCreateDevice(physical, &device_info, nullptr, &device);
    if (result != VK_SUCCESS) {
        error = "vkCreateDevice failed: " + resultString(result);
        return false;
    }
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family;
    result = vkCreateCommandPool(device, &pool_info, nullptr, &command_pool);
    if (result != VK_SUCCESS) {
        error = "vkCreateCommandPool failed: " + resultString(result);
        return false;
    }

    VkCommandBufferAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = command_pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(device, &allocation, &command_buffer);
    if (result != VK_SUCCESS) {
        error = "vkAllocateCommandBuffers failed: " + resultString(result);
        return false;
    }
    return true;
}

bool VulkanEngine::Impl::ensureHostBuffer(HostBuffer& buffer,
                                          VkDeviceSize size,
                                          VkBufferUsageFlags usage,
                                          std::string& error) {
    size = std::max<VkDeviceSize>(size, 4);
    if (buffer.buffer != VK_NULL_HANDLE && buffer.size >= size &&
        (buffer.usage & usage) == usage) {
        return true;
    }
    destroyHostBuffer(buffer);

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = vkCreateBuffer(device, &buffer_info, nullptr,
                                     &buffer.buffer);
    if (result != VK_SUCCESS) {
        error = "vkCreateBuffer failed: " + resultString(result);
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer.buffer, &requirements);
    std::uint32_t memory_type = findMemoryType(
        memory_properties, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    buffer.coherent = memory_type != UINT32_MAX;
    if (!buffer.coherent) {
        memory_type = findMemoryType(
            memory_properties, requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    }
    if (memory_type == UINT32_MAX) {
        error = "no host-visible memory type for staging buffer";
        return false;
    }
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type;
    result = vkAllocateMemory(device, &allocation, nullptr, &buffer.memory);
    if (result != VK_SUCCESS) {
        error = "vkAllocateMemory failed for host buffer: " +
                resultString(result);
        return false;
    }
    result = vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0);
    if (result != VK_SUCCESS) {
        error = "vkBindBufferMemory failed: " + resultString(result);
        return false;
    }
    result = vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0,
                         &buffer.mapped);
    if (result != VK_SUCCESS) {
        error = "vkMapMemory failed: " + resultString(result);
        return false;
    }
    buffer.size = size;
    buffer.usage = usage;
    return true;
}

bool VulkanEngine::Impl::ensureDeviceBuffer(DeviceBuffer& buffer,
                                            VkDeviceSize size,
                                            VkBufferUsageFlags usage,
                                            std::string& error) {
    size = std::max<VkDeviceSize>(size, 4);
    if (buffer.buffer != VK_NULL_HANDLE && buffer.size >= size &&
        (buffer.usage & usage) == usage) {
        return true;
    }
    destroyDeviceBuffer(buffer);
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result =
        vkCreateBuffer(device, &buffer_info, nullptr, &buffer.buffer);
    if (result != VK_SUCCESS) {
        error = "vkCreateBuffer failed for device buffer: " +
                resultString(result);
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer.buffer, &requirements);
    std::uint32_t memory_type = findMemoryType(
        memory_properties, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        memory_type = findMemoryType(memory_properties,
                                     requirements.memoryTypeBits, 0);
    }
    if (memory_type == UINT32_MAX) {
        error = "no memory type for device buffer";
        return false;
    }
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type;
    result = vkAllocateMemory(device, &allocation, nullptr, &buffer.memory);
    if (result != VK_SUCCESS) {
        error = "vkAllocateMemory failed for device buffer: " +
                resultString(result);
        return false;
    }
    result = vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0);
    if (result != VK_SUCCESS) {
        error = "vkBindBufferMemory failed for device buffer: " +
                resultString(result);
        return false;
    }
    buffer.size = size;
    buffer.usage = usage;
    return true;
}

bool VulkanEngine::Impl::createImage(std::string& error) {
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult result = vkCreateImage(device, &image_info, nullptr, &image);
    if (result != VK_SUCCESS) {
        error = "vkCreateImage failed: " + resultString(result);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image, &requirements);
    std::uint32_t memory_type = findMemoryType(
        memory_properties, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        memory_type = findMemoryType(memory_properties,
                                     requirements.memoryTypeBits, 0);
    }
    if (memory_type == UINT32_MAX) {
        error = "no memory type for offscreen image";
        return false;
    }
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type;
    result = vkAllocateMemory(device, &allocation, nullptr, &image_memory);
    if (result != VK_SUCCESS) {
        error = "vkAllocateMemory failed for offscreen image: " +
                resultString(result);
        return false;
    }
    result = vkBindImageMemory(device, image, image_memory, 0);
    if (result != VK_SUCCESS) {
        error = "vkBindImageMemory failed: " + resultString(result);
        return false;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    result = vkCreateImageView(device, &view_info, nullptr, &image_view);
    if (result != VK_SUCCESS) {
        error = "vkCreateImageView failed: " + resultString(result);
        return false;
    }
    return true;
}

bool VulkanEngine::Impl::createRenderPass(std::string& error) {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference color_reference{};
    color_reference.attachment = 0;
    color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_reference;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount =
        static_cast<std::uint32_t>(dependencies.size());
    render_pass_info.pDependencies = dependencies.data();
    VkResult result =
        vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass);
    if (result != VK_SUCCESS) {
        error = "vkCreateRenderPass failed: " + resultString(result);
        return false;
    }

    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &image_view;
    framebuffer_info.width = width;
    framebuffer_info.height = height;
    framebuffer_info.layers = 1;
    result =
        vkCreateFramebuffer(device, &framebuffer_info, nullptr, &framebuffer);
    if (result != VK_SUCCESS) {
        error = "vkCreateFramebuffer failed: " + resultString(result);
        return false;
    }
    return true;
}

bool VulkanEngine::Impl::createDescriptors(std::string& error) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                         VK_SHADER_STAGE_GEOMETRY_BIT |
                         VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;
    VkResult result = vkCreateDescriptorSetLayout(
        device, &layout_info, nullptr, &descriptor_layout);
    if (result != VK_SUCCESS) {
        error = "vkCreateDescriptorSetLayout failed: " +
                resultString(result);
        return false;
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    result =
        vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool);
    if (result != VK_SUCCESS) {
        error = "vkCreateDescriptorPool failed: " + resultString(result);
        return false;
    }
    VkDescriptorSetAllocateInfo set_info{};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_info.descriptorPool = descriptor_pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &descriptor_layout;
    result = vkAllocateDescriptorSets(device, &set_info, &descriptor_set);
    if (result != VK_SUCCESS) {
        error = "vkAllocateDescriptorSets failed: " + resultString(result);
        return false;
    }
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_layout;
    result = vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr,
                                    &pipeline_layout);
    if (result != VK_SUCCESS) {
        error = "vkCreatePipelineLayout failed: " + resultString(result);
        return false;
    }
    return true;
}

bool VulkanEngine::Impl::createPipeline(const std::string& shader_dir,
                                        bool capture, std::string& error) {
    auto loadModule = [&](const std::string& name,
                          VkShaderModule& module) -> bool {
        const std::filesystem::path path =
            std::filesystem::path(shader_dir) / (name + ".spv");
        std::vector<std::uint8_t> code;
        if (!readBinaryFile(path, code) || code.empty() ||
            code.size() % sizeof(std::uint32_t) != 0U) {
            error = "cannot load SPIR-V shader: " + path.string();
            return false;
        }
        VkShaderModuleCreateInfo module_info{};
        module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module_info.codeSize = code.size();
        module_info.pCode =
            reinterpret_cast<const std::uint32_t*>(code.data());
        const VkResult result =
            vkCreateShaderModule(device, &module_info, nullptr, &module);
        if (result != VK_SUCCESS) {
            error = "vkCreateShaderModule failed for " + path.string() + ": " +
                    resultString(result);
            return false;
        }
        return true;
    };

    VkShaderModule vertex_module = VK_NULL_HANDLE;
    VkShaderModule geometry_module = VK_NULL_HANDLE;
    VkShaderModule fragment_module = VK_NULL_HANDLE;
    const bool modules_ok =
        loadModule(capture ? "capture.vert" : "replay.vert", vertex_module) &&
        (capture ? loadModule("capture.geom", geometry_module)
                 : loadModule("output.frag", fragment_module));
    if (!modules_ok) {
        if (vertex_module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, vertex_module, nullptr);
        }
        if (geometry_module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, geometry_module, nullptr);
        }
        if (fragment_module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, fragment_module, nullptr);
        }
        return false;
    }
    std::array<VkPipelineShaderStageCreateInfo, 3> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_module;
    stages[0].pName = "main";
    std::uint32_t stage_count = 1;
    if (capture) {
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        stages[1].module = geometry_module;
        stages[1].pName = "main";
        stage_count = 2;
    }
    if (!capture) {
        stages[stage_count].sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[stage_count].module = fragment_module;
        stages[stage_count].pName = "main";
        ++stage_count;
    }

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = kInputVertexStride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 3> attributes{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
    attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};
    attributes[2] = {2, 0, VK_FORMAT_R32_UINT, 24};
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (capture) {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount =
            static_cast<std::uint32_t>(attributes.size());
        vertex_input.pVertexAttributeDescriptions = attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = capture ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                                      : VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkViewport viewport{};
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, {width, height}};
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;
    rasterization.rasterizerDiscardEnable =
        capture ? VK_TRUE : VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend_attachment;
    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = stage_count;
    pipeline_info.pStages = stages.data();
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result = vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        error = "vkCreateGraphicsPipelines failed: " + resultString(result);
    } else if (capture) {
        capture_pipeline = pipeline;
    } else {
        replay_pipeline = pipeline;
    }

    vkDestroyShaderModule(device, vertex_module, nullptr);
    if (geometry_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, geometry_module, nullptr);
    }
    if (fragment_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, fragment_module, nullptr);
    }
    return result == VK_SUCCESS;
}

bool VulkanEngine::Impl::createPipelines(const std::string& shader_dir,
                                         std::string& error) {
    if (shader_dir.empty()) {
        error = "SPIR-V shader directory is empty";
        return false;
    }
    return createPipeline(shader_dir, true, error) &&
           createPipeline(shader_dir, false, error);
}

bool VulkanEngine::Impl::createResources(std::uint32_t requested_width,
                                         std::uint32_t requested_height,
                                         const std::string& shader_dir,
                                         std::string& error) {
    width = std::max<std::uint32_t>(requested_width, 1U);
    height = std::max<std::uint32_t>(requested_height, 1U);
    return createImage(error) && createRenderPass(error) &&
           createDescriptors(error) && createPipelines(shader_dir, error);
}

bool VulkanEngine::Impl::updateCaptureDescriptor(std::string& error) {
    if (capture_buffer.buffer == VK_NULL_HANDLE ||
        descriptor_set == VK_NULL_HANDLE) {
        error = "capture buffer or descriptor set is unavailable";
        return false;
    }
    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = capture_buffer.buffer;
    buffer_info.offset = 0;
    buffer_info.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &buffer_info;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return true;
}

bool VulkanEngine::geometryPass(const Material& material,
                                std::vector<std::uint8_t>& capture_bytes,
                                std::string& error) {
    capture_bytes.clear();
    if (!usesVulkan() || !impl_ || impl_->device == VK_NULL_HANDLE) {
        error = "geometryPass requires an initialized Vulkan backend";
        return false;
    }
    const VkDeviceSize vertex_bytes =
        static_cast<VkDeviceSize>(material.vertices.size()) *
        kInputVertexStride;
    const VkDeviceSize capture_size =
        static_cast<VkDeviceSize>(material.vertices.size()) *
        kCaptureRecordBytes;
    if (!impl_->ensureHostBuffer(impl_->vertex_buffer, vertex_bytes,
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, error) ||
        !impl_->ensureDeviceBuffer(
            impl_->capture_buffer, capture_size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            error) ||
        !impl_->ensureHostBuffer(impl_->staging_buffer, capture_size,
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT, error)) {
        return false;
    }
    std::memcpy(impl_->vertex_buffer.mapped, material.vertices.data(),
                static_cast<std::size_t>(vertex_bytes));
    if (!impl_->updateCaptureDescriptor(error)) {
        return false;
    }

    VkResult result =
        vkResetCommandBuffer(impl_->command_buffer, 0);
    if (result != VK_SUCCESS) {
        error = "vkResetCommandBuffer failed: " + resultString(result);
        return false;
    }
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(impl_->command_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        error = "vkBeginCommandBuffer failed: " + resultString(result);
        return false;
    }

    vkCmdFillBuffer(impl_->command_buffer, impl_->capture_buffer.buffer, 0,
                    capture_size, 0);
    VkBufferMemoryBarrier fill_barrier{};
    fill_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    fill_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    fill_barrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    fill_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fill_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fill_barrier.buffer = impl_->capture_buffer.buffer;
    fill_barrier.offset = 0;
    fill_barrier.size = capture_size;
    vkCmdPipelineBarrier(impl_->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0, nullptr, 1,
                         &fill_barrier, 0, nullptr);

    VkClearValue clear{};
    clear.color = {{0.03f, 0.04f, 0.07f, 1.0f}};
    VkRenderPassBeginInfo render_begin{};
    render_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_begin.renderPass = impl_->render_pass;
    render_begin.framebuffer = impl_->framebuffer;
    render_begin.renderArea = {{0, 0}, {impl_->width, impl_->height}};
    render_begin.clearValueCount = 1;
    render_begin.pClearValues = &clear;
    vkCmdBeginRenderPass(impl_->command_buffer, &render_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(impl_->command_buffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      impl_->capture_pipeline);
    vkCmdBindDescriptorSets(impl_->command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            impl_->pipeline_layout, 0, 1,
                            &impl_->descriptor_set, 0, nullptr);
    const VkDeviceSize vertex_offset = 0;
    vkCmdBindVertexBuffers(impl_->command_buffer, 0, 1,
                           &impl_->vertex_buffer.buffer, &vertex_offset);
    vkCmdDraw(impl_->command_buffer,
              static_cast<std::uint32_t>(material.vertices.size()), 1, 0, 0);
    vkCmdEndRenderPass(impl_->command_buffer);

    VkBufferMemoryBarrier copy_barrier{};
    copy_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    copy_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    copy_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    copy_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    copy_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    copy_barrier.buffer = impl_->capture_buffer.buffer;
    copy_barrier.offset = 0;
    copy_barrier.size = capture_size;
    vkCmdPipelineBarrier(impl_->command_buffer,
                         VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                         &copy_barrier, 0, nullptr);
    VkBufferCopy copy_region{};
    copy_region.size = capture_size;
    vkCmdCopyBuffer(impl_->command_buffer, impl_->capture_buffer.buffer,
                    impl_->staging_buffer.buffer, 1, &copy_region);
    VkBufferMemoryBarrier host_barrier{};
    host_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    host_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host_barrier.buffer = impl_->staging_buffer.buffer;
    host_barrier.offset = 0;
    host_barrier.size = capture_size;
    vkCmdPipelineBarrier(impl_->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                         &host_barrier, 0, nullptr);
    result = vkEndCommandBuffer(impl_->command_buffer);
    if (result != VK_SUCCESS) {
        error = "vkEndCommandBuffer failed: " + resultString(result);
        return false;
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &impl_->command_buffer;
    result = vkQueueSubmit(impl_->queue, 1, &submit, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        error = "vkQueueSubmit failed for geometry pass: " +
                resultString(result);
        return false;
    }
    result = vkQueueWaitIdle(impl_->queue);
    if (result != VK_SUCCESS) {
        error = "vkQueueWaitIdle failed for geometry pass: " +
                resultString(result);
        return false;
    }
    VkMappedMemoryRange capture_range{};
    capture_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    capture_range.memory = impl_->staging_buffer.memory;
    capture_range.offset = 0;
    capture_range.size = VK_WHOLE_SIZE;
    vkInvalidateMappedMemoryRanges(impl_->device, 1, &capture_range);
    capture_bytes.resize(static_cast<std::size_t>(capture_size));
    std::memcpy(capture_bytes.data(), impl_->staging_buffer.mapped,
                static_cast<std::size_t>(capture_size));
    return true;
}

bool VulkanEngine::compositePass(
    const std::vector<std::uint8_t>& capture_bytes,
    std::vector<std::uint8_t>& rgba, std::string& error) {
    rgba.clear();
    if (!usesVulkan() || !impl_ || impl_->device == VK_NULL_HANDLE) {
        error = "compositePass requires an initialized Vulkan backend";
        return false;
    }
    if (capture_bytes.empty() ||
        capture_bytes.size() % kCaptureRecordBytes != 0U) {
        error = "geometry capture is empty or not record-aligned";
        return false;
    }
    if (!impl_->ensureDeviceBuffer(
            impl_->capture_buffer, capture_bytes.size(),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            error) ||
        !impl_->ensureHostBuffer(impl_->staging_buffer, capture_bytes.size(),
                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT, error) ||
        !impl_->ensureHostBuffer(
            impl_->readback_buffer,
            static_cast<VkDeviceSize>(impl_->width) * impl_->height * 4ULL,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT, error)) {
        return false;
    }
    std::memcpy(impl_->staging_buffer.mapped, capture_bytes.data(),
                capture_bytes.size());
    if (!impl_->staging_buffer.coherent) {
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = impl_->staging_buffer.memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        vkFlushMappedMemoryRanges(impl_->device, 1, &range);
    }
    if (!impl_->updateCaptureDescriptor(error)) {
        return false;
    }

    VkResult result = vkResetCommandBuffer(impl_->command_buffer, 0);
    if (result != VK_SUCCESS) {
        error = "vkResetCommandBuffer failed: " + resultString(result);
        return false;
    }
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(impl_->command_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        error = "vkBeginCommandBuffer failed: " + resultString(result);
        return false;
    }

    VkBufferMemoryBarrier host_to_transfer{};
    host_to_transfer.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    host_to_transfer.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    host_to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    host_to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host_to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host_to_transfer.buffer = impl_->staging_buffer.buffer;
    host_to_transfer.offset = 0;
    host_to_transfer.size = capture_bytes.size();
    vkCmdPipelineBarrier(impl_->command_buffer,
                         VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                         &host_to_transfer, 0, nullptr);
    VkBufferCopy upload_copy{};
    upload_copy.size = capture_bytes.size();
    vkCmdCopyBuffer(impl_->command_buffer, impl_->staging_buffer.buffer,
                    impl_->capture_buffer.buffer, 1, &upload_copy);
    VkBufferMemoryBarrier upload_barrier{};
    upload_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    upload_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    upload_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    upload_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    upload_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    upload_barrier.buffer = impl_->capture_buffer.buffer;
    upload_barrier.offset = 0;
    upload_barrier.size = capture_bytes.size();
    vkCmdPipelineBarrier(impl_->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 1,
                         &upload_barrier, 0, nullptr);

    VkClearValue clear{};
    clear.color = {{0.03f, 0.04f, 0.07f, 1.0f}};
    VkRenderPassBeginInfo render_begin{};
    render_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_begin.renderPass = impl_->render_pass;
    render_begin.framebuffer = impl_->framebuffer;
    render_begin.renderArea = {{0, 0}, {impl_->width, impl_->height}};
    render_begin.clearValueCount = 1;
    render_begin.pClearValues = &clear;
    vkCmdBeginRenderPass(impl_->command_buffer, &render_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(impl_->command_buffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      impl_->replay_pipeline);
    vkCmdBindDescriptorSets(impl_->command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            impl_->pipeline_layout, 0, 1,
                            &impl_->descriptor_set, 0, nullptr);
    const std::uint32_t record_count =
        static_cast<std::uint32_t>(capture_bytes.size() /
                                   kCaptureRecordBytes);
    vkCmdDraw(impl_->command_buffer, record_count * 2U, 1, 0, 0);
    vkCmdEndRenderPass(impl_->command_buffer);

    VkBufferImageCopy image_copy{};
    image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_copy.imageSubresource.layerCount = 1;
    image_copy.imageExtent = {impl_->width, impl_->height, 1};
    vkCmdCopyImageToBuffer(impl_->command_buffer, impl_->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           impl_->readback_buffer.buffer, 1, &image_copy);
    VkBufferMemoryBarrier readback_barrier{};
    readback_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    readback_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    readback_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    readback_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readback_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readback_barrier.buffer = impl_->readback_buffer.buffer;
    readback_barrier.offset = 0;
    readback_barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(impl_->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                         &readback_barrier, 0, nullptr);
    result = vkEndCommandBuffer(impl_->command_buffer);
    if (result != VK_SUCCESS) {
        error = "vkEndCommandBuffer failed: " + resultString(result);
        return false;
    }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &impl_->command_buffer;
    result = vkQueueSubmit(impl_->queue, 1, &submit, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        error = "vkQueueSubmit failed for composite pass: " +
                resultString(result);
        return false;
    }
    result = vkQueueWaitIdle(impl_->queue);
    if (result != VK_SUCCESS) {
        error = "vkQueueWaitIdle failed for composite pass: " +
                resultString(result);
        return false;
    }
    VkMappedMemoryRange readback_range{};
    readback_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    readback_range.memory = impl_->readback_buffer.memory;
    readback_range.offset = 0;
    readback_range.size = VK_WHOLE_SIZE;
    vkInvalidateMappedMemoryRanges(impl_->device, 1, &readback_range);
    const std::size_t byte_count =
        static_cast<std::size_t>(impl_->width) * impl_->height * 4ULL;
    rgba.resize(byte_count);
    std::memcpy(rgba.data(), impl_->readback_buffer.mapped, byte_count);
    return true;
}

void VulkanEngine::shutdown() {
    if (impl_) {
        impl_->destroy();
        impl_.reset();
    }
}

bool VulkanEngine::initialize(const EngineOptions& options,
                              const Material& material, ResourcePlan& plan,
                              std::string& error) {
    shutdown();
    impl_ = std::make_unique<Impl>();
    plan_ = ResourcePlan{};
    plan_.requested_mode = options.requested_mode;
    plan_.cpu_fallback_allowed = options.allow_cpu_fallback;
    plan_.input_bytes =
        static_cast<std::uint64_t>(material.vertices.size()) * kInputVertexStride +
        material.byte_size;
    plan_.capture_bytes =
        static_cast<std::uint64_t>(material.vertices.size()) * kCaptureRecordBytes;
    plan_.capture_records = static_cast<std::uint32_t>(material.vertices.size());
    plan_.width = options.requested_width == 0 ? 512 : options.requested_width;
    plan_.height = options.requested_height == 0 ? 512 : options.requested_height;

    if (!isValidMode(options.requested_mode)) {
        error = "invalid resource mode: " + options.requested_mode;
        plan = makeCpuFallbackPlan(options, material, error);
        return options.allow_cpu_fallback;
    }

    auto degrade = [&](const std::string& reason) {
        plan_.reasons.push_back(reason);
        plan_.backend = "cpu";
        plan_.selected_mode = "cpu";
        plan_.degraded = true;
        plan_.width = options.requested_width;
        plan_.height = options.requested_height;
        plan_.image_bytes =
            static_cast<std::uint64_t>(plan_.width) * plan_.height * 4ULL;
        plan_.required_bytes =
            plan_.input_bytes + plan_.capture_bytes + plan_.image_bytes * 2ULL +
            kMiB;
        impl_->destroy();
        plan = plan_;
        if (!options.allow_cpu_fallback) {
            error = reason;
            return false;
        }
        error = reason;
        return true;
    };

    if (!impl_->createInstanceAndPhysical(error)) {
        return degrade(error);
    }
    plan_.device_name = impl_->properties.deviceName;
    plan_.driver_name = "driver-version-" +
                        std::to_string(impl_->properties.driverVersion);
    plan_.api_version = apiVersionString(impl_->properties.apiVersion);
    plan_.geometry_shader_supported =
        impl_->features.geometryShader == VK_TRUE;
    plan_.vertex_pipeline_stores_supported =
        impl_->features.vertexPipelineStoresAndAtomics == VK_TRUE;
    plan_.max_storage_buffer_range = impl_->limits.maxStorageBufferRange;

    std::uint64_t heap_bytes = 0;
    for (std::uint32_t index = 0;
         index < impl_->memory_properties.memoryHeapCount; ++index) {
        const auto& heap = impl_->memory_properties.memoryHeaps[index];
        if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0U) {
            heap_bytes = std::max(heap_bytes, heap.size);
        }
    }
    if (heap_bytes == 0) {
        for (std::uint32_t index = 0;
             index < impl_->memory_properties.memoryHeapCount; ++index) {
            heap_bytes = std::max(
                heap_bytes,
                static_cast<std::uint64_t>(
                    impl_->memory_properties.memoryHeaps[index].size));
        }
    }
    plan_.heap_bytes = heap_bytes;

    const std::uint64_t reserve = std::min(64ULL * kMiB, heap_bytes / 8ULL);
    const std::uint64_t default_budget =
        heap_bytes > reserve ? heap_bytes - reserve : heap_bytes;
    std::uint64_t budget = options.memory_budget_bytes == 0
                               ? default_budget
                               : options.memory_budget_bytes;
    const std::uint64_t full_required = planRequiredBytes(
        material, plan_.width, plan_.height);
    if (options.force_low_memory) {
        budget = std::max(1ULL, full_required / 2ULL);
    }
    plan_.memory_budget_bytes = budget;
    plan_.required_bytes = full_required;
    plan_.image_bytes =
        static_cast<std::uint64_t>(plan_.width) * plan_.height * 4ULL;

    if (options.requested_mode == "cpu") {
        return degrade("CPU degradation requested explicitly");
    }
    if (!plan_.geometry_shader_supported) {
        return degrade("geometry shader feature is unavailable");
    }
    if (!plan_.vertex_pipeline_stores_supported) {
        return degrade(
            "vertex/geometry storage-buffer stores are unavailable");
    }
    if (plan_.capture_bytes > plan_.max_storage_buffer_range) {
        return degrade("geometry capture exceeds maxStorageBufferRange");
    }

    bool needs_compact = false;
    if (options.requested_mode == "compact") {
        needs_compact = true;
    } else if (full_required > budget) {
        if (options.requested_mode == "full") {
            return degrade("full mode exceeds the admitted memory budget");
        }
        needs_compact = true;
    }
    if (needs_compact) {
        plan_.width = std::min<std::uint32_t>(options.requested_width, 256U);
        plan_.height = std::min<std::uint32_t>(options.requested_height, 256U);
        plan_.width = std::max<std::uint32_t>(plan_.width, 64U);
        plan_.height = std::max<std::uint32_t>(plan_.height, 64U);
        plan_.required_bytes =
            planRequiredBytes(material, plan_.width, plan_.height);
        plan_.image_bytes =
            static_cast<std::uint64_t>(plan_.width) * plan_.height * 4ULL;
        if (plan_.required_bytes > budget) {
            return degrade("compact mode still exceeds the memory budget");
        }
        plan_.reasons.push_back("reduced offscreen extent to fit memory budget");
        plan_.selected_mode = "compact";
        plan_.degraded = true;
    } else {
        plan_.selected_mode = "full";
        plan_.degraded = false;
    }

    if (!impl_->createDevice(error)) {
        return degrade(error);
    }
    if (!impl_->createResources(plan_.width, plan_.height, options.shader_dir,
                                error)) {
        return degrade(error);
    }
    plan_.backend = "vulkan";
    plan = plan_;
    return true;
}

}  // namespace inspect
