#include "vk_context.h"
#include <dlfcn.h>
#include <cstring>

const char* vkResultName(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
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
        case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
        default: return "VK_RESULT_UNLISTED";
    }
}

VkException::VkException(VkResult r, const std::string& what)
    : std::runtime_error(std::string(vkResultName(r)) + ": " + what), result(r) {}

VkContext::VkContext() = default;

VkContext::~VkContext() {
    shutdown();
    if (lib_) dlclose(lib_);
}

void VkContext::loadGlobal() {
    lib_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib_) throw VkException(VK_ERROR_INITIALIZATION_FAILED,
                                 std::string("dlopen libvulkan.so.1 failed: ") + dlerror());
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(lib_, "vkGetInstanceProcAddr"));
    if (!vkGetInstanceProcAddr)
        throw VkException(VK_ERROR_INITIALIZATION_FAILED, "vkGetInstanceProcAddr missing");
    auto gp = [this](const char* n) { return vkGetInstanceProcAddr(nullptr, n); };
    vkEnumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(gp("vkEnumerateInstanceVersion"));
    vkEnumerateInstanceExtensionProperties = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(gp("vkEnumerateInstanceExtensionProperties"));
    vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(gp("vkCreateInstance"));
    if (!vkEnumerateInstanceExtensionProperties || !vkCreateInstance)
        throw VkException(VK_ERROR_INITIALIZATION_FAILED, "global Vulkan functions missing");
    if (vkEnumerateInstanceVersion) {
        uint32_t v = VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion(&v) == VK_SUCCESS) loaderVersion_ = v;
    }
}

std::vector<VkExtensionProperties> VkContext::enumerateInstanceExtensions() {
    uint32_t count = 0;
    VkResult r = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    if (r != VK_SUCCESS) throw VkException(r, "vkEnumerateInstanceExtensionProperties(count)");
    std::vector<VkExtensionProperties> out(count);
    r = vkEnumerateInstanceExtensionProperties(nullptr, &count, out.data());
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) throw VkException(r, "vkEnumerateInstanceExtensionProperties(list)");
    out.resize(count);
    return out;
}

void VkContext::createInstance(const std::vector<const char*>& extensions) {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "terrain-chunk-experiment";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "terrain-exp";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
    VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
    if (r != VK_SUCCESS) throw VkException(r, "vkCreateInstance");
    auto ip = [this](const char* n) { return vkGetInstanceProcAddr(instance_, n); };
    vkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(ip("vkDestroyInstance"));
    vkEnumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(ip("vkEnumeratePhysicalDevices"));
    vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(ip("vkGetPhysicalDeviceProperties"));
    vkGetPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(ip("vkGetPhysicalDeviceMemoryProperties"));
    vkGetPhysicalDeviceQueueFamilyProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(ip("vkGetPhysicalDeviceQueueFamilyProperties"));
    vkEnumerateDeviceExtensionProperties = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(ip("vkEnumerateDeviceExtensionProperties"));
    vkCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(ip("vkCreateDevice"));
    vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(ip("vkGetDeviceProcAddr"));
}

void VkContext::pickPhysicalDevice() {
    uint32_t count = 0;
    VkResult r = vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (r != VK_SUCCESS) throw VkException(r, "vkEnumeratePhysicalDevices(count)");
    if (count == 0) throw VkException(VK_ERROR_INITIALIZATION_FAILED, "no Vulkan physical device");
    std::vector<VkPhysicalDevice> devs(count);
    r = vkEnumeratePhysicalDevices(instance_, &count, devs.data());
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) throw VkException(r, "vkEnumeratePhysicalDevices(list)");
    phys_ = devs[0];
    std::memset(&props_, 0, sizeof(props_));
    vkGetPhysicalDeviceProperties(phys_, &props_);
    vkGetPhysicalDeviceMemoryProperties(phys_, &memProps_);
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qfCount, nullptr);
    if (qfCount == 0) throw VkException(VK_ERROR_INITIALIZATION_FAILED, "no queue family");
    std::vector<VkQueueFamilyProperties> qf(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qfCount, qf.data());
    queueFamily_ = 0;
    for (uint32_t i = 0; i < qfCount; ++i) {
        if (qf[i].queueCount > 0) { queueFamily_ = i; break; }
    }
}

std::vector<VkExtensionProperties> VkContext::enumerateDeviceExtensions() {
    uint32_t count = 0;
    VkResult r = vkEnumerateDeviceExtensionProperties(phys_, nullptr, &count, nullptr);
    if (r != VK_SUCCESS) throw VkException(r, "vkEnumerateDeviceExtensionProperties(count)");
    std::vector<VkExtensionProperties> out(count);
    r = vkEnumerateDeviceExtensionProperties(phys_, nullptr, &count, out.data());
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) throw VkException(r, "vkEnumerateDeviceExtensionProperties(list)");
    out.resize(count);
    return out;
}

void VkContext::createDevice(const std::vector<const char*>& extensions) {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = queueFamily_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    dci.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
    VkDevice dev = nullptr;
    VkResult r = vkCreateDevice(phys_, &dci, nullptr, &dev);
    if (r != VK_SUCCESS) throw VkException(r, "vkCreateDevice");
    device_ = dev;
    auto dp = [this](const char* n) { return vkGetDeviceProcAddr(device_, n); };
    vkDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(dp("vkDestroyDevice"));
    vkCreateBuffer = reinterpret_cast<PFN_vkCreateBuffer>(dp("vkCreateBuffer"));
    vkDestroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(dp("vkDestroyBuffer"));
    vkGetBufferMemoryRequirements = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(dp("vkGetBufferMemoryRequirements"));
    vkAllocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(dp("vkAllocateMemory"));
    vkFreeMemory = reinterpret_cast<PFN_vkFreeMemory>(dp("vkFreeMemory"));
    vkBindBufferMemory = reinterpret_cast<PFN_vkBindBufferMemory>(dp("vkBindBufferMemory"));
    vkMapMemory = reinterpret_cast<PFN_vkMapMemory>(dp("vkMapMemory"));
    vkUnmapMemory = reinterpret_cast<PFN_vkUnmapMemory>(dp("vkUnmapMemory"));
}

uint32_t VkContext::findHostVisibleMemoryType(uint32_t typeBits) const {
    const VkMemoryPropertyFlags want =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memProps_.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (memProps_.memoryTypes[i].propertyFlags & want) == want) return i;
    }
    throw VkException(VK_ERROR_INITIALIZATION_FAILED, "no host-visible coherent memory type");
}

size_t VkContext::upload(const void* data, size_t size) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    GpuBuffer gb;
    gb.size = size;
    VkResult r = vkCreateBuffer(device_, &bci, nullptr, &gb.buffer);
    if (r != VK_SUCCESS) throw VkException(r, "vkCreateBuffer");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, gb.buffer, &req);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findHostVisibleMemoryType(req.memoryTypeBits);
    r = vkAllocateMemory(device_, &mai, nullptr, &gb.memory);
    if (r != VK_SUCCESS) { vkDestroyBuffer(device_, gb.buffer, nullptr); throw VkException(r, "vkAllocateMemory"); }
    r = vkBindBufferMemory(device_, gb.buffer, gb.memory, 0);
    if (r != VK_SUCCESS) throw VkException(r, "vkBindBufferMemory");
    void* mapped = nullptr;
    r = vkMapMemory(device_, gb.memory, 0, size, 0, &mapped);
    if (r != VK_SUCCESS) throw VkException(r, "vkMapMemory");
    std::memcpy(mapped, data, size);
    vkUnmapMemory(device_, gb.memory);
    buffers_.push_back(gb);
    return buffers_.size() - 1;
}

std::vector<uint8_t> VkContext::readback(size_t slot) {
    const GpuBuffer& gb = buffers_.at(slot);
    void* mapped = nullptr;
    VkResult r = vkMapMemory(device_, gb.memory, 0, gb.size, 0, &mapped);
    if (r != VK_SUCCESS) throw VkException(r, "vkMapMemory(readback)");
    std::vector<uint8_t> out(static_cast<size_t>(gb.size));
    std::memcpy(out.data(), mapped, out.size());
    vkUnmapMemory(device_, gb.memory);
    return out;
}

void VkContext::destroyDevice() {
    if (!device_) return;
    for (auto& gb : buffers_) {
        if (gb.buffer) vkDestroyBuffer(device_, gb.buffer, nullptr);
        if (gb.memory) vkFreeMemory(device_, gb.memory, nullptr);
    }
    buffers_.clear();
    vkDestroyDevice(device_, nullptr);
    device_ = nullptr;
}

void VkContext::shutdown() {
    destroyDevice();
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = nullptr;
    }
}
