#include "vulkan_context.h"
#include "common.h"

#include <cstdio>
#include <vector>

namespace vox {

void vk_check(VkResult result, const char* what) {
  if (result != VK_SUCCESS) {
    throw SlicerError(std::string("Vulkan failure (") +
                      std::to_string(static_cast<int>(result)) + ") at " +
                      what);
  }
}

VulkanContext::VulkanContext(uint32_t width, uint32_t height)
    : width_(width), height_(height) {
  if (!glfwInit()) {
    throw SlicerError("glfwInit failed");
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // headless review pipeline
  window_ = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height),
                             "voxel-slicer", nullptr, nullptr);
  if (!window_) throw SlicerError("glfwCreateWindow failed");

  uint32_t ext_count = 0;
  const char** exts = glfwGetRequiredInstanceExtensions(&ext_count);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "voxel-slicer";
  app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app.pEngineName = "review-pipeline";
  app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = ext_count;
  ici.ppEnabledExtensionNames = exts;
  vk_check(vkCreateInstance(&ici, nullptr, &instance_), "vkCreateInstance");

  uint32_t dev_count = 0;
  vkEnumeratePhysicalDevices(instance_, &dev_count, nullptr);
  if (dev_count == 0) throw SlicerError("no Vulkan physical device");
  std::vector<VkPhysicalDevice> devs(dev_count);
  vkEnumeratePhysicalDevices(instance_, &dev_count, devs.data());

  // Prefer a discrete GPU, accept anything with a graphics queue.
  physical_ = devs[0];
  for (auto dev : devs) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(dev, &props);
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      physical_ = dev;
      break;
    }
  }
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(physical_, &props);
  device_name_ = props.deviceName;
  api_version_ = props.apiVersion;

  uint32_t qf_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_, &qf_count, nullptr);
  std::vector<VkQueueFamilyProperties> qfs(qf_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_, &qf_count, qfs.data());
  for (uint32_t i = 0; i < qf_count; ++i) {
    if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queue_family_ = i;
      break;
    }
  }

  float priority = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = queue_family_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &priority;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  vk_check(vkCreateDevice(physical_, &dci, nullptr, &device_),
           "vkCreateDevice");
  vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

  VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cpci.queueFamilyIndex = queue_family_;
  vk_check(vkCreateCommandPool(device_, &cpci, nullptr, &command_pool_),
           "vkCreateCommandPool");
}

VkCommandBuffer VulkanContext::begin_oneshot() {
  VkCommandBufferAllocateInfo ai{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = command_pool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd;
  vk_check(vkAllocateCommandBuffers(device_, &ai, &cmd),
           "vkAllocateCommandBuffers");
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vk_check(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer");
  return cmd;
}

void VulkanContext::end_oneshot(VkCommandBuffer cmd) {
  vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vk_check(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE),
           "vkQueueSubmit");
  vk_check(vkQueueWaitIdle(queue_), "vkQueueWaitIdle");
  vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
}

VulkanContext::~VulkanContext() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
    if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);
    vkDestroyDevice(device_, nullptr);
  }
  if (instance_) vkDestroyInstance(instance_, nullptr);
  if (window_) glfwDestroyWindow(window_);
  glfwTerminate();
}

} // namespace vox
