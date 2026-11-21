#include "vulkan_probe.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <chrono>
#include <ctime>
#include <stdexcept>
#include <vector>

namespace probe {

namespace {

std::string nowIso8601() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

void requireVk(VkResult r, const char* what) {
    if (r != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed, VkResult=" +
                                 std::to_string(static_cast<int>(r)));
    }
}

// RAII guard so temporary GLFW/Vulkan state is always released.
struct GlfwGuard {
    GlfwGuard() {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
        if (glfwInit() != GLFW_TRUE) {
            throw std::runtime_error("glfwInit failed");
        }
    }
    ~GlfwGuard() { glfwTerminate(); }
};

struct InstanceGuard {
    VkInstance instance = VK_NULL_HANDLE;
    ~InstanceGuard() {
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
    }
};

} // namespace

std::string formatVersion(uint32_t v) {
    return std::to_string(VK_VERSION_MAJOR(v)) + "." +
           std::to_string(VK_VERSION_MINOR(v)) + "." +
           std::to_string(VK_VERSION_PATCH(v));
}

ProbeResult runProbe() {
    ProbeResult out;
    GlfwGuard glfw;

    out.glfwVersion = glfwGetVersionString();
    out.glfwVulkanSupported = glfwVulkanSupported() == GLFW_TRUE;
    if (!out.glfwVulkanSupported) {
        throw std::runtime_error("glfwVulkanSupported() returned false");
    }

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    for (uint32_t i = 0; glfwExts != nullptr && i < glfwExtCount; ++i) {
        out.requiredInstanceExtensions.emplace_back(glfwExts[i]);
    }

    uint32_t instanceVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&instanceVersion) != VK_SUCCESS) {
        instanceVersion = VK_API_VERSION_1_0; // pre-1.1 loader
    }
    out.instanceSnapshot.source = "vkEnumerateInstanceVersion";
    out.instanceSnapshot.apiVersion = instanceVersion;
    out.instanceSnapshot.capturedAt = nowIso8601();

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "aa_history_verify";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "none";
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = glfwExtCount;
    ci.ppEnabledExtensionNames = glfwExts;

    InstanceGuard inst;
    requireVk(vkCreateInstance(&ci, nullptr, &inst.instance), "vkCreateInstance");

    uint32_t devCount = 0;
    requireVk(vkEnumeratePhysicalDevices(inst.instance, &devCount, nullptr),
              "vkEnumeratePhysicalDevices(count)");
    if (devCount == 0) {
        throw std::runtime_error("no Vulkan physical devices found");
    }
    std::vector<VkPhysicalDevice> devices(devCount);
    requireVk(vkEnumeratePhysicalDevices(inst.instance, &devCount, devices.data()),
              "vkEnumeratePhysicalDevices(list)");

    for (VkPhysicalDevice dev : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(dev, &props);
        VersionSnapshot snap;
        snap.source = "VkPhysicalDeviceProperties";
        snap.apiVersion = props.apiVersion;
        snap.driverVersion = props.driverVersion;
        snap.deviceName = props.deviceName;
        snap.capturedAt = nowIso8601();
        out.deviceSnapshots.push_back(std::move(snap));
    }

    const VkPhysicalDevice first = devices.front();

    uint32_t extCount = 0;
    requireVk(vkEnumerateDeviceExtensionProperties(first, nullptr, &extCount, nullptr),
              "vkEnumerateDeviceExtensionProperties(count)");
    std::vector<VkExtensionProperties> exts(extCount);
    requireVk(vkEnumerateDeviceExtensionProperties(first, nullptr, &extCount, exts.data()),
              "vkEnumerateDeviceExtensionProperties(list)");
    for (const auto& e : exts) {
        out.deviceExtensions.push_back({e.extensionName, e.specVersion});
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(first, &props);
    out.msaa.colorSampleCounts = props.limits.framebufferColorSampleCounts;
    out.msaa.depthSampleCounts = props.limits.framebufferDepthSampleCounts;

    return out;
}

} // namespace probe
