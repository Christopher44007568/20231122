#include "vk_common.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <iostream>
#include <set>

namespace skr {
namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    std::cerr << "[vulkan] " << (data->pMessage ? data->pMessage : "") << '\n';
    return VK_FALSE;
}

} // namespace

VulkanContext::VulkanContext(uint32_t width, uint32_t height)
        : width_(width), height_(height) {
#if defined(GLFW_PLATFORM_NULL)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
#endif
    if (!glfwInit())
        throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindow_ = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height),
                                   "skinning-review", nullptr, nullptr);
    if (!glfwWindow_)
        throw std::runtime_error("glfwCreateWindow failed (null platform)");

    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
    const bool validationAvailable = [&] {
        for (const auto& l : layers)
            if (std::string(l.layerName) == "VK_LAYER_KHRONOS_validation")
                return true;
        return false;
    }();
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";

    uint32_t extCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + extCount);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "gpu-skinning-review";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "none";
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    if (validationAvailable) {
        ci.enabledLayerCount = 1;
        ci.ppEnabledLayerNames = &validationLayer;
    }
    VK_CHECK(vkCreateInstance(&ci, nullptr, &instance_));

    if (validationAvailable) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        VkDebugUtilsMessengerCreateInfoEXT dci{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dci.pfnUserCallback = debugCallback;
        if (create) create(instance_, &dci, nullptr, &debugMessenger_);
    }

    uint32_t gpuCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance_, &gpuCount, nullptr));
    if (gpuCount == 0) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    VK_CHECK(vkEnumeratePhysicalDevices(instance_, &gpuCount, gpus.data()));

    // Prefer a discrete GPU, but accept any device with a graphics queue
    // (lavapipe / llvmpipe is the normal headless fallback here).
    int bestScore = -1;
    for (VkPhysicalDevice gpu : gpus) {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qfCount, qfs.data());
        int score = -1;
        uint32_t family = 0;
        for (uint32_t q = 0; q < qfCount; ++q) {
            if (qfs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                score = 10;
                family = q;
                break;
            }
        }
        if (score < 0) continue;
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(gpu, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 100;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 40;
        if (score > bestScore) {
            bestScore = score;
            physicalDevice_ = gpu;
            queueFamily_ = family;
            VkPhysicalDeviceProperties p{};
            vkGetPhysicalDeviceProperties(gpu, &p);
            deviceName_ = p.deviceName;
        }
    }
    if (!physicalDevice_) throw std::runtime_error("no graphics-capable Vulkan device");

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queueFamily_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci2{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci2.queueCreateInfoCount = 1;
    dci2.pQueueCreateInfos = &qci;
    VK_CHECK(vkCreateDevice(physicalDevice_, &dci2, nullptr, &device_));
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queueFamily_;
    VK_CHECK(vkCreateCommandPool(device_, &pci, nullptr, &commandPool_));
}

VulkanContext::~VulkanContext() {
    if (device_) vkDeviceWaitIdle(device_);
    if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
    if (device_) vkDestroyDevice(device_, nullptr);
    if (debugMessenger_) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance_, debugMessenger_, nullptr);
    }
    if (instance_) vkDestroyInstance(instance_, nullptr);
    if (glfwWindow_) glfwDestroyWindow(static_cast<GLFWwindow*>(glfwWindow_));
    glfwTerminate();
}

VkCommandBuffer VulkanContext::beginOneShot() const {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = commandPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &cmd));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    return cmd;
}

void VulkanContext::endAndWait(VkCommandBuffer cmd) const {
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue_));
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
}

VkShaderModule VulkanContext::loadShader(const uint32_t* code,
                                         size_t codeSizeWords) const {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = codeSizeWords * sizeof(uint32_t);
    ci.pCode = code;
    VkShaderModule module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device_, &ci, nullptr, &module));
    return module;
}

uint32_t VulkanContext::findMemoryType(uint32_t typeBits,
                                       VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("no suitable memory type");
}

} // namespace skr
