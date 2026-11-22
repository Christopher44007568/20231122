#include "vk_context.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstring>

namespace tar {

VkContext::VkContext(Log& log) : log_(log) {
  // GLFW side: prefer the Null platform so the tool runs headless; fall
  // back to the default platform if a display server happens to exist.
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
  if (glfwInit() != GLFW_TRUE) {
    log.Warn("GLFW null-platform init failed, retrying with default platform");
    glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
    if (glfwInit() != GLFW_TRUE) {
      throw PipelineError("glfwInit failed");
    }
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  window_ = glfwCreateWindow(64, 64, "texarray-review", nullptr, nullptr);
  if (!window_) {
    log.Warn("GLFW window creation failed; continuing without a window");
  }

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "texarray-review";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  VK_CHECK(vkCreateInstance(&ici, nullptr, &instance_));

  uint32_t count = 0;
  VK_CHECK(vkEnumeratePhysicalDevices(instance_, &count, nullptr));
  if (count == 0) throw PipelineError("no Vulkan physical devices");
  std::vector<VkPhysicalDevice> devices(count);
  VK_CHECK(vkEnumeratePhysicalDevices(instance_, &count, devices.data()));
  physical_ = devices[0];
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(physical_, &props);
  device_name_ = props.deviceName;
  log_.Info(std::string("Vulkan device: ") + device_name_);

  uint32_t qf_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_, &qf_count, nullptr);
  std::vector<VkQueueFamilyProperties> qfs(qf_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_, &qf_count, qfs.data());
  bool found = false;
  for (uint32_t i = 0; i < qf_count; ++i) {
    if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queue_family_ = i;
      found = true;
      break;
    }
  }
  if (!found) throw PipelineError("no graphics queue family");

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = queue_family_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  VK_CHECK(vkCreateDevice(physical_, &dci, nullptr, &device_));
  vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

  VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpci.queueFamilyIndex = queue_family_;
  cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VK_CHECK(vkCreateCommandPool(device_, &cpci, nullptr, &cmd_pool_));
}

VkContext::~VkContext() {
  if (device_) {
    vkDeviceWaitIdle(device_);
    if (cmd_pool_) vkDestroyCommandPool(device_, cmd_pool_, nullptr);
    vkDestroyDevice(device_, nullptr);
  }
  if (instance_) vkDestroyInstance(instance_, nullptr);
  if (window_) glfwDestroyWindow(window_);
  glfwTerminate();
}

void VkContext::PollEvents() { glfwPollEvents(); }

uint32_t VkContext::FindMemoryType(uint32_t type_bits,
                                   VkMemoryPropertyFlags props) const {
  VkPhysicalDeviceMemoryProperties mem;
  vkGetPhysicalDeviceMemoryProperties(physical_, &mem);
  for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) &&
        (mem.memoryTypes[i].propertyFlags & props) == props) {
      return i;
    }
  }
  throw PipelineError("no suitable memory type");
}

VkCommandBuffer VkContext::BeginCommands() {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = cmd_pool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd;
  VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &cmd));
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
  return cmd;
}

void VkContext::EndCommands(VkCommandBuffer cmd) {
  VK_CHECK(vkEndCommandBuffer(cmd));
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  VK_CHECK(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE));
  VK_CHECK(vkQueueWaitIdle(queue_));
  vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);
}

}  // namespace tar
