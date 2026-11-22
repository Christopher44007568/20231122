#include "vk.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstring>
#include <vector>

namespace pcd {

static uint32_t find_memory_type(VkContext& ctx, uint32_t type_bits,
                                 VkMemoryPropertyFlags props) {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(ctx.phys, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags & props) == props)
      return i;
  }
  die("no suitable Vulkan memory type");
}

VkContext vk_create(bool with_surface_exts) {
  VkContext ctx;
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "pcdclip";
  app.apiVersion = VK_API_VERSION_1_1;

  std::vector<const char*> exts;
  if (with_surface_exts) {
    uint32_t n = 0;
    const char** glfw_exts = glfwGetRequiredInstanceExtensions(&n);
    if (!glfw_exts)
      die("GLFW reports no Vulkan instance extensions (no display?)");
    exts.assign(glfw_exts, glfw_exts + n);
  }
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = static_cast<uint32_t>(exts.size());
  ici.ppEnabledExtensionNames = exts.data();
  VK_CHECK(vkCreateInstance(&ici, nullptr, &ctx.instance));

  uint32_t dev_count = 0;
  VK_CHECK(vkEnumeratePhysicalDevices(ctx.instance, &dev_count, nullptr));
  if (dev_count == 0) die("no Vulkan physical devices");
  std::vector<VkPhysicalDevice> devs(dev_count);
  VK_CHECK(vkEnumeratePhysicalDevices(ctx.instance, &dev_count, devs.data()));

  // Prefer a discrete/integrated GPU, fall back to anything (lavapipe).
  ctx.phys = devs[0];
  for (auto d : devs) {
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(d, &p);
    if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
        p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
      ctx.phys = d;
      break;
    }
  }
  vkGetPhysicalDeviceProperties(ctx.phys, &ctx.props);
  ctx.device_name = ctx.props.deviceName;

  uint32_t qf_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx.phys, &qf_count, nullptr);
  std::vector<VkQueueFamilyProperties> qfs(qf_count);
  vkGetPhysicalDeviceQueueFamilyProperties(ctx.phys, &qf_count, qfs.data());
  bool found = false;
  for (uint32_t i = 0; i < qf_count; ++i) {
    VkQueueFlags need = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT;
    if ((qfs[i].queueFlags & need) == need) {
      ctx.queue_family = i;
      found = true;
      break;
    }
  }
  if (!found) die("no queue family with compute+graphics");

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = ctx.queue_family;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  VK_CHECK(vkCreateDevice(ctx.phys, &dci, nullptr, &ctx.device));
  vkGetDeviceQueue(ctx.device, ctx.queue_family, 0, &ctx.queue);

  VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpi.queueFamilyIndex = ctx.queue_family;
  cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VK_CHECK(vkCreateCommandPool(ctx.device, &cpi, nullptr, &ctx.cmd_pool));
  return ctx;
}

void vk_destroy(VkContext& ctx) {
  if (ctx.device) {
    vkDeviceWaitIdle(ctx.device);
    vkDestroyCommandPool(ctx.device, ctx.cmd_pool, nullptr);
    vkDestroyDevice(ctx.device, nullptr);
  }
  if (ctx.instance) vkDestroyInstance(ctx.instance, nullptr);
  ctx = VkContext{};
}

VkBuffer2 vk_buffer(VkContext& ctx, VkDeviceSize size,
                    VkBufferUsageFlags usage, bool host_visible) {
  VkBuffer2 b;
  b.size = size;
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VK_CHECK(vkCreateBuffer(ctx.device, &bci, nullptr, &b.buf));
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(ctx.device, b.buf, &req);
  VkMemoryPropertyFlags props =
      host_visible
          ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
          : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = find_memory_type(ctx, req.memoryTypeBits, props);
  VK_CHECK(vkAllocateMemory(ctx.device, &mai, nullptr, &b.mem));
  VK_CHECK(vkBindBufferMemory(ctx.device, b.buf, b.mem, 0));
  return b;
}

void vk_buffer_destroy(VkContext& ctx, VkBuffer2& b) {
  if (b.buf) vkDestroyBuffer(ctx.device, b.buf, nullptr);
  if (b.mem) vkFreeMemory(ctx.device, b.mem, nullptr);
  b = VkBuffer2{};
}

void vk_upload(VkContext& ctx, VkBuffer2& dst, const void* data,
               VkDeviceSize size) {
  void* mapped = nullptr;
  VK_CHECK(vkMapMemory(ctx.device, dst.mem, 0, size, 0, &mapped));
  std::memcpy(mapped, data, static_cast<size_t>(size));
  vkUnmapMemory(ctx.device, dst.mem);
}

void vk_download(VkContext& ctx, VkBuffer2& src, void* data,
                 VkDeviceSize size) {
  void* mapped = nullptr;
  VK_CHECK(vkMapMemory(ctx.device, src.mem, 0, size, 0, &mapped));
  std::memcpy(data, mapped, static_cast<size_t>(size));
  vkUnmapMemory(ctx.device, src.mem);
}

VkShaderModule vk_shader(VkContext& ctx, const std::string& spv_path) {
  auto bytes = read_file(spv_path);
  if (bytes.empty() || bytes.size() % 4 != 0)
    die("bad SPIR-V file: " + spv_path);
  VkShaderModuleCreateInfo sci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  sci.codeSize = bytes.size();
  sci.pCode = reinterpret_cast<const uint32_t*>(bytes.data());
  VkShaderModule mod = VK_NULL_HANDLE;
  VK_CHECK(vkCreateShaderModule(ctx.device, &sci, nullptr, &mod));
  return mod;
}

void vk_one_shot(VkContext& ctx,
                 const std::function<void(VkCommandBuffer)>& record) {
  VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cai.commandPool = ctx.cmd_pool;
  cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cai.commandBufferCount = 1;
  VkCommandBuffer cmd;
  VK_CHECK(vkAllocateCommandBuffers(ctx.device, &cai, &cmd));
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
  record(cmd);
  VK_CHECK(vkEndCommandBuffer(cmd));
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  VK_CHECK(vkQueueSubmit(ctx.queue, 1, &si, VK_NULL_HANDLE));
  VK_CHECK(vkQueueWaitIdle(ctx.queue));
  vkFreeCommandBuffers(ctx.device, ctx.cmd_pool, 1, &cmd);
}

} // namespace pcd
