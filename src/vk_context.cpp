#include "vk_context.h"

#include <cstring>
#include <stdexcept>

namespace {

void vkCheck(VkResult r, const char* what) {
  if (r != VK_SUCCESS)
    throw std::runtime_error(std::string(what) + " failed with VkResult " + std::to_string(r));
}

}  // namespace

VkContext::VkContext() {
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "topo_diag";
  app.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  vkCheck(vkCreateInstance(&ici, nullptr, &instance_), "vkCreateInstance");

  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance_, &count, nullptr);
  if (count == 0) throw std::runtime_error("no Vulkan physical devices available");
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance_, &count, devices.data());

  // Prefer a discrete/GPU device, but accept anything with a graphics queue
  // (lavapipe's CPU device is what headless CI uses).
  for (auto dev : devices) {
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount, qfs.data());
    for (uint32_t i = 0; i < qfCount; ++i) {
      if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        physicalDevice_ = dev;
        queueFamily_ = i;
        break;
      }
    }
    if (physicalDevice_) {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(dev, &props);
      deviceName_ = props.deviceName;
      if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) break;
    }
  }
  if (!physicalDevice_) throw std::runtime_error("no device with a graphics queue found");

  float priority = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = queueFamily_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &priority;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  vkCheck(vkCreateDevice(physicalDevice_, &dci, nullptr, &device_), "vkCreateDevice");
  vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

  VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cpi.queueFamilyIndex = queueFamily_;
  vkCheck(vkCreateCommandPool(device_, &cpi, nullptr, &commandPool_), "vkCreateCommandPool");
}

VkContext::~VkContext() {
  if (device_) {
    vkDeviceWaitIdle(device_);
    if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
    vkDestroyDevice(device_, nullptr);
  }
  if (instance_) vkDestroyInstance(instance_, nullptr);
}

uint32_t VkContext::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
  VkPhysicalDeviceMemoryProperties mem;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mem);
  for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props) return i;
  }
  throw std::runtime_error("no suitable memory type");
}
