#include "gpu.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <cstring>

namespace gpu {

struct GpuContext::Impl {
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    VkQueryPool query_pool = VK_NULL_HANDLE;
    VkBuffer src_buffer = VK_NULL_HANDLE;
    VkBuffer dst_buffer = VK_NULL_HANDLE;
    VkDeviceMemory src_mem = VK_NULL_HANDLE;
    VkDeviceMemory dst_mem = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool glfw_up = false;
};

static constexpr uint32_t kQueryBegin = 0;
static constexpr uint32_t kQueryEnd = 1;
static constexpr VkDeviceSize kWorkBytes = 4 * 1024 * 1024;

bool GpuContext::init(std::string& error) {
    impl_ = new Impl();

    if (!glfwInit()) {
        // Headless fallback: GLFW 3.4 null platform still gives us a real
        // (offscreen) window object without a display server.
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
        if (!glfwInit()) {
            error = "glfwInit failed on default and null platform";
            return false;
        }
        device_.glfw_null_platform = true;
    }
    impl_->glfw_up = true;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    impl_->window = glfwCreateWindow(64, 64, "gpu-ts-inspect", nullptr, nullptr);
    if (!impl_->window) {
        error = "glfwCreateWindow failed";
        return false;
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "gpu_ts_inspect";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, nullptr, &impl_->instance) != VK_SUCCESS) {
        error = "vkCreateInstance failed";
        return false;
    }

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(impl_->instance, &count, nullptr);
    if (count == 0) {
        error = "no Vulkan physical devices";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(impl_->instance, &count, devices.data());

    for (VkPhysicalDevice pd : devices) {
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, qprops.data());
        for (uint32_t q = 0; q < qcount; q++) {
            bool usable = (qprops[q].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                          qprops[q].timestampValidBits > 0;
            if (usable) {
                impl_->physical = pd;
                device_.queue_family = q;
                device_.timestamp_valid_bits = qprops[q].timestampValidBits;
                break;
            }
        }
        if (impl_->physical) break;
    }
    if (!impl_->physical) {
        error = "no queue family with compute + timestamp support";
        return false;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(impl_->physical, &props);
    device_.name = props.deviceName;
    device_.api_version = props.apiVersion;
    device_.timestamp_period_ns = props.limits.timestampPeriod;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = device_.queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (vkCreateDevice(impl_->physical, &dci, nullptr, &impl_->device) != VK_SUCCESS) {
        error = "vkCreateDevice failed";
        return false;
    }
    vkGetDeviceQueue(impl_->device, device_.queue_family, 0, &impl_->queue);

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = device_.queue_family;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(impl_->device, &cpci, nullptr, &impl_->cmd_pool);

    VkQueryPoolCreateInfo qpi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qpi.queryCount = 2;
    vkCreateQueryPool(impl_->device, &qpi, nullptr, &impl_->query_pool);

    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(impl_->device, &fci, nullptr, &impl_->fence);

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = kWorkBytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    vkCreateBuffer(impl_->device, &bci, nullptr, &impl_->src_buffer);
    vkCreateBuffer(impl_->device, &bci, nullptr, &impl_->dst_buffer);

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(impl_->device, impl_->src_buffer, &req);
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(impl_->physical, &mem_props);
    uint32_t mem_type = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((req.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            mem_type = i;
            break;
        }
    }
    if (mem_type == UINT32_MAX) mem_type = 0;
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mem_type;
    vkAllocateMemory(impl_->device, &mai, nullptr, &impl_->src_mem);
    vkAllocateMemory(impl_->device, &mai, nullptr, &impl_->dst_mem);
    vkBindBufferMemory(impl_->device, impl_->src_buffer, impl_->src_mem, 0);
    vkBindBufferMemory(impl_->device, impl_->dst_buffer, impl_->dst_mem, 0);
    return true;
}

bool GpuContext::sample_frame(bool skip_end_timestamp, FrameSample& out, std::string& error) {
    VkDevice dev = impl_->device;

    vkResetFences(dev, 1, &impl_->fence);

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = impl_->cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(dev, &cbai, &cmd);

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    // Host-side vkResetQueryPool needs Vulkan 1.2 / VK_EXT_host_query_reset;
    // the in-command-buffer reset is core since 1.0.
    vkCmdResetQueryPool(cmd, impl_->query_pool, 0, 2);

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, impl_->query_pool, kQueryBegin);

    VkBufferCopy region{};
    region.size = kWorkBytes;
    for (int pass = 0; pass < 4; pass++) {
        vkCmdCopyBuffer(cmd, impl_->src_buffer, impl_->dst_buffer, 1, &region);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
        vkCmdCopyBuffer(cmd, impl_->dst_buffer, impl_->src_buffer, 1, &region);
    }

    if (!skip_end_timestamp) {
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, impl_->query_pool, kQueryEnd);
    }
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    if (vkQueueSubmit(impl_->queue, 1, &submit, impl_->fence) != VK_SUCCESS) {
        error = "vkQueueSubmit failed";
        vkFreeCommandBuffers(dev, impl_->cmd_pool, 1, &cmd);
        return false;
    }
    vkWaitForFences(dev, 1, &impl_->fence, VK_TRUE, UINT64_MAX);

    struct Stamp {
        uint64_t value;
        uint64_t available;
    } stamps[2];
    VkResult qr = vkGetQueryPoolResults(
        dev, impl_->query_pool, 0, 2, sizeof(stamps), stamps, sizeof(Stamp),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    vkFreeCommandBuffers(dev, impl_->cmd_pool, 1, &cmd);
    if (qr != VK_SUCCESS && qr != VK_NOT_READY) {
        error = "vkGetQueryPoolResults failed";
        return false;
    }
    out.ts_begin = stamps[kQueryBegin].value;
    out.ts_end = stamps[kQueryEnd].value;
    out.availability = (stamps[kQueryBegin].available ? 1u : 0u) |
                       (stamps[kQueryEnd].available ? 2u : 0u);
    // The value of an unavailable query is undefined; record 0 so the log
    // never carries meaningless bytes, and let avail bits speak.
    if (!(out.availability & 1u)) out.ts_begin = 0;
    if (!(out.availability & 2u)) out.ts_end = 0;
    return true;
}

void GpuContext::shutdown() {
    if (!impl_) return;
    if (impl_->device) {
        vkDeviceWaitIdle(impl_->device);
        vkDestroyBuffer(impl_->device, impl_->src_buffer, nullptr);
        vkDestroyBuffer(impl_->device, impl_->dst_buffer, nullptr);
        vkFreeMemory(impl_->device, impl_->src_mem, nullptr);
        vkFreeMemory(impl_->device, impl_->dst_mem, nullptr);
        vkDestroyFence(impl_->device, impl_->fence, nullptr);
        vkDestroyQueryPool(impl_->device, impl_->query_pool, nullptr);
        vkDestroyCommandPool(impl_->device, impl_->cmd_pool, nullptr);
        vkDestroyDevice(impl_->device, nullptr);
    }
    if (impl_->instance) vkDestroyInstance(impl_->instance, nullptr);
    if (impl_->window) glfwDestroyWindow(impl_->window);
    if (impl_->glfw_up) glfwTerminate();
    delete impl_;
    impl_ = nullptr;
}

}  // namespace gpu
