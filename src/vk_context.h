#pragma once
#include "vkmini.h"
#include <string>
#include <vector>
#include <stdexcept>

struct VkException : std::runtime_error {
    VkResult result;
    explicit VkException(VkResult r, const std::string& what);
};

struct GpuBuffer {
    VkBuffer buffer = nullptr;
    VkDeviceMemory memory = nullptr;
    VkDeviceSize size = 0;
};

// Runtime-loaded Vulkan context: instance + one physical device + one logical
// device + host-visible buffer upload/readback. No surface/swapchain needed.
class VkContext {
public:
    VkContext();
    ~VkContext();
    VkContext(const VkContext&) = delete;
    VkContext& operator=(const VkContext&) = delete;

    void loadGlobal(); // dlopen loader, resolve global functions
    std::vector<VkExtensionProperties> enumerateInstanceExtensions();
    void createInstance(const std::vector<const char*>& extensions);
    void pickPhysicalDevice();
    std::vector<VkExtensionProperties> enumerateDeviceExtensions();
    void createDevice(const std::vector<const char*>& extensions); // throws VkException

    // Upload bytes to a fresh host-visible buffer; returns buffer slot index.
    size_t upload(const void* data, size_t size);
    // Read back a previously uploaded buffer for verification.
    std::vector<uint8_t> readback(size_t slot);
    size_t bufferCount() const { return buffers_.size(); }

    void destroyDevice();
    void shutdown();

    const VkPhysicalDeviceProperties& deviceProps() const { return props_; }
    uint32_t loaderVersion() const { return loaderVersion_; }

private:
    void* lib_ = nullptr;
    VkInstance instance_ = nullptr;
    VkPhysicalDevice phys_ = nullptr;
    VkDevice device_ = nullptr;
    VkPhysicalDeviceProperties props_{};
    VkPhysicalDeviceMemoryProperties memProps_{};
    uint32_t queueFamily_ = 0;
    uint32_t loaderVersion_ = 0;
    std::vector<GpuBuffer> buffers_;

    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
    PFN_vkCreateInstance vkCreateInstance = nullptr;
    PFN_vkDestroyInstance vkDestroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = nullptr;
    PFN_vkCreateDevice vkCreateDevice = nullptr;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
    PFN_vkDestroyDevice vkDestroyDevice = nullptr;
    PFN_vkCreateBuffer vkCreateBuffer = nullptr;
    PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
    PFN_vkAllocateMemory vkAllocateMemory = nullptr;
    PFN_vkFreeMemory vkFreeMemory = nullptr;
    PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
    PFN_vkMapMemory vkMapMemory = nullptr;
    PFN_vkUnmapMemory vkUnmapMemory = nullptr;

    uint32_t findHostVisibleMemoryType(uint32_t typeBits) const;
};
