#include "vk_context.h"

#include <cstdio>
#include <cstring>

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    std::fprintf(stderr, "[vk] %s\n", data->pMessage);
  return VK_FALSE;
}

bool VkContext::init(bool enableValidation,
                     const std::vector<const char*>& extraInstanceExts) {
  validation_ = enableValidation;

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "shadow-cache-analyzer";
  app.apiVersion = VK_API_VERSION_1_1;

  std::vector<const char*> layers;
  std::vector<const char*> exts;
  if (validation_) {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> avail(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, avail.data());
    bool hasValidation = false;
    for (auto& l : avail)
      if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
        hasValidation = true;
    if (hasValidation) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    } else {
      std::fprintf(stderr, "[vk] validation layer not present, disabled\n");
      validation_ = false;
    }
  }
  for (const char* e : extraInstanceExts) exts.push_back(e);

  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  ici.enabledLayerCount = (uint32_t)layers.size();
  ici.ppEnabledLayerNames = layers.data();
  ici.enabledExtensionCount = (uint32_t)exts.size();
  ici.ppEnabledExtensionNames = exts.data();
  if (vkCreateInstance(&ici, nullptr, &instance_) != VK_SUCCESS) {
    std::fprintf(stderr, "vkCreateInstance failed\n");
    return false;
  }

  if (validation_) {
    auto createDbg = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
    if (createDbg) {
      VkDebugUtilsMessengerCreateInfoEXT dci{
          VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
      dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      dci.pfnUserCallback = debugCallback;
      createDbg(instance_, &dci, nullptr, &debugMessenger_);
    }
  }

  uint32_t physCount = 0;
  vkEnumeratePhysicalDevices(instance_, &physCount, nullptr);
  if (physCount == 0) {
    std::fprintf(stderr, "no Vulkan physical devices\n");
    return false;
  }
  std::vector<VkPhysicalDevice> devs(physCount);
  vkEnumeratePhysicalDevices(instance_, &physCount, devs.data());

  // Prefer a discrete/GPU device, but accept anything (llvmpipe included).
  phys_ = devs[0];
  for (auto d : devs) {
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(d, &p);
    if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      phys_ = d;
      break;
    }
  }
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(phys_, &props);
  deviceName_ = props.deviceName;
  timestampPeriodNs_ = props.limits.timestampPeriod;

  uint32_t qCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qCount, nullptr);
  std::vector<VkQueueFamilyProperties> qprops(qCount);
  vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qCount, qprops.data());
  bool found = false;
  for (uint32_t i = 0; i < qCount; ++i) {
    if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queueFamily_ = i;
      timestampValidBits_ = qprops[i].timestampValidBits;
      found = true;
      break;
    }
  }
  if (!found) {
    std::fprintf(stderr, "no graphics queue family\n");
    return false;
  }

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = queueFamily_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  dci.enabledExtensionCount = 1;
  dci.ppEnabledExtensionNames = devExts;
  if (vkCreateDevice(phys_, &dci, nullptr, &device_) != VK_SUCCESS) {
    std::fprintf(stderr, "vkCreateDevice failed\n");
    return false;
  }
  vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

  VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpi.queueFamilyIndex = queueFamily_;
  cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  if (vkCreateCommandPool(device_, &cpi, nullptr, &cmdPool_) != VK_SUCCESS)
    return false;
  return true;
}

void VkContext::shutdown() {
  if (device_) {
    vkDeviceWaitIdle(device_);
    if (cmdPool_) vkDestroyCommandPool(device_, cmdPool_, nullptr);
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
  if (instance_) {
    if (debugMessenger_) {
      auto destroyDbg = (PFN_vkDestroyDebugUtilsMessengerEXT)
          vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
      if (destroyDbg) destroyDbg(instance_, debugMessenger_, nullptr);
    }
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

VkCommandBuffer VkContext::beginCommands() {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = cmdPool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd;
  vkAllocateCommandBuffers(device_, &ai, &cmd);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  return cmd;
}

void VkContext::endCommands(VkCommandBuffer cmd) {
  vkEndCommandBuffer(cmd);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue_);
  vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
}

uint32_t VkContext::findMemoryType(uint32_t typeBits,
                                   VkMemoryPropertyFlags props) const {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(phys_, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    if ((typeBits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags & props) == props)
      return i;
  return UINT32_MAX;
}

void GpuImage::destroy(VkDevice dev) {
  if (view) vkDestroyImageView(dev, view, nullptr);
  if (image) vkDestroyImage(dev, image, nullptr);
  if (memory) vkFreeMemory(dev, memory, nullptr);
  *this = GpuImage{};
}

void GpuBuffer::destroy(VkDevice dev) {
  if (buffer) vkDestroyBuffer(dev, buffer, nullptr);
  if (memory) vkFreeMemory(dev, memory, nullptr);
  *this = GpuBuffer{};
}

bool createImage(VkContext& ctx, uint32_t w, uint32_t h, VkFormat format,
                 VkImageUsageFlags usage, VkImageAspectFlags aspect,
                 GpuImage& out) {
  VkDevice dev = ctx.device();
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = format;
  ii.extent = {w, h, 1};
  ii.mipLevels = 1;
  ii.arrayLayers = 1;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = usage;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(dev, &ii, nullptr, &out.image) != VK_SUCCESS) return false;

  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(dev, out.image, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex =
      ctx.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (ai.memoryTypeIndex == UINT32_MAX) return false;
  if (vkAllocateMemory(dev, &ai, nullptr, &out.memory) != VK_SUCCESS)
    return false;
  vkBindImageMemory(dev, out.image, out.memory, 0);

  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = out.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = format;
  vi.subresourceRange = {aspect, 0, 1, 0, 1};
  if (vkCreateImageView(dev, &vi, nullptr, &out.view) != VK_SUCCESS)
    return false;
  out.width = w;
  out.height = h;
  out.format = format;
  return true;
}

bool createBuffer(VkContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags props, GpuBuffer& out) {
  VkDevice dev = ctx.device();
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(dev, &bi, nullptr, &out.buffer) != VK_SUCCESS)
    return false;
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(dev, out.buffer, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = ctx.findMemoryType(req.memoryTypeBits, props);
  if (ai.memoryTypeIndex == UINT32_MAX) return false;
  if (vkAllocateMemory(dev, &ai, nullptr, &out.memory) != VK_SUCCESS)
    return false;
  vkBindBufferMemory(dev, out.buffer, out.memory, 0);
  out.size = size;
  if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    vkMapMemory(dev, out.memory, 0, size, 0, &out.mapped);
  return true;
}

void transitionImage(VkCommandBuffer cmd, VkImage image,
                     VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkImageAspectFlags aspect) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = oldLayout;
  b.newLayout = newLayout;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {aspect, 0, 1, 0, 1};
  VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    b.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }
  if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    b.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  } else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    b.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    b.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }
  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1,
                       &b);
}
