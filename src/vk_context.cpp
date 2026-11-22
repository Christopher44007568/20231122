#include "vk_context.h"
#include <cstdio>
#include <cstring>

static bool hasExt(const std::vector<VkExtensionProperties>& exts,
                   const std::string& name) {
  for (const auto& e : exts)
    if (name == e.extensionName) return true;
  return false;
}

bool vkInit(VkContext& ctx, const std::vector<std::string>& instanceExts,
            bool enableUnknownExtControl, std::string& err,
            const std::vector<std::string>& deviceExts) {
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> availInst(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, availInst.data());
  for (const auto& e : availInst) ctx.extControl.instanceExts.push_back(e.extensionName);
  ctx.extControl.unknownInstanceExtSupported =
      hasExt(availInst, ctx.extControl.unknownInstanceExt);

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "csm_replay_lab";
  app.apiVersion = VK_API_VERSION_1_1;

  std::vector<const char*> instExtPtrs;
  for (const auto& e : instanceExts) instExtPtrs.push_back(e.c_str());

  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = uint32_t(instExtPtrs.size());
  ici.ppEnabledExtensionNames = instExtPtrs.data();
  VkResult r = vkCreateInstance(&ici, nullptr, &ctx.instance);
  if (r != VK_SUCCESS) {
    err = "vkCreateInstance failed: " + std::to_string(r);
    return false;
  }

  uint32_t devCount = 0;
  vkEnumeratePhysicalDevices(ctx.instance, &devCount, nullptr);
  if (devCount == 0) {
    err = "no Vulkan physical devices";
    return false;
  }
  std::vector<VkPhysicalDevice> devs(devCount);
  vkEnumeratePhysicalDevices(ctx.instance, &devCount, devs.data());

  for (auto d : devs) {
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(d, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(d, &qfCount, qfs.data());
    for (uint32_t i = 0; i < qfCount; ++i) {
      if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        ctx.phys = d;
        ctx.queueFamily = i;
        break;
      }
    }
    if (ctx.phys != VK_NULL_HANDLE) break;
  }
  if (ctx.phys == VK_NULL_HANDLE) {
    err = "no graphics queue family found";
    return false;
  }

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(ctx.phys, &props);
  ctx.deviceName = props.deviceName;
  ctx.apiVersion = props.apiVersion;
  vkGetPhysicalDeviceMemoryProperties(ctx.phys, &ctx.memProps);

  uint32_t dextCount = 0;
  vkEnumerateDeviceExtensionProperties(ctx.phys, nullptr, &dextCount, nullptr);
  std::vector<VkExtensionProperties> availDev(dextCount);
  vkEnumerateDeviceExtensionProperties(ctx.phys, nullptr, &dextCount, availDev.data());
  for (const auto& e : availDev) ctx.extControl.deviceExts.push_back(e.extensionName);
  ctx.extControl.unknownDeviceExtSupported =
      hasExt(availDev, ctx.extControl.unknownDeviceExt);

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = ctx.queueFamily;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;

  if (enableUnknownExtControl) {
    const char* badExt = ctx.extControl.unknownDeviceExt.c_str();
    VkDeviceCreateInfo badDci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    badDci.queueCreateInfoCount = 1;
    badDci.pQueueCreateInfos = &qci;
    badDci.enabledExtensionCount = 1;
    badDci.ppEnabledExtensionNames = &badExt;
    VkDevice tmp = VK_NULL_HANDLE;
    ctx.extControl.deviceCreateWithUnknownExt =
        vkCreateDevice(ctx.phys, &badDci, nullptr, &tmp);
    if (tmp != VK_NULL_HANDLE) vkDestroyDevice(tmp, nullptr);
  }

  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  std::vector<const char*> devExtPtrs;
  for (const auto& e : deviceExts) devExtPtrs.push_back(e.c_str());
  dci.enabledExtensionCount = uint32_t(devExtPtrs.size());
  dci.ppEnabledExtensionNames = devExtPtrs.data();
  ctx.extControl.deviceCreateClean =
      vkCreateDevice(ctx.phys, &dci, nullptr, &ctx.device);
  if (ctx.extControl.deviceCreateClean != VK_SUCCESS) {
    err = "vkCreateDevice failed: " + std::to_string(ctx.extControl.deviceCreateClean);
    return false;
  }

  vkGetDeviceQueue(ctx.device, ctx.queueFamily, 0, &ctx.queue);

  VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpci.queueFamilyIndex = ctx.queueFamily;
  cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  if (vkCreateCommandPool(ctx.device, &cpci, nullptr, &ctx.cmdPool) != VK_SUCCESS) {
    err = "vkCreateCommandPool failed";
    return false;
  }
  return true;
}

void vkDestroy(VkContext& ctx) {
  if (ctx.device) {
    vkDeviceWaitIdle(ctx.device);
    if (ctx.cmdPool) vkDestroyCommandPool(ctx.device, ctx.cmdPool, nullptr);
    vkDestroyDevice(ctx.device, nullptr);
  }
  if (ctx.instance) vkDestroyInstance(ctx.instance, nullptr);
  ctx = VkContext{};
}

uint32_t findMemoryType(const VkContext& ctx, uint32_t typeBits,
                        VkMemoryPropertyFlags props) {
  for (uint32_t i = 0; i < ctx.memProps.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) &&
        (ctx.memProps.memoryTypes[i].propertyFlags & props) == props)
      return i;
  }
  return UINT32_MAX;
}

GpuBuffer createBuffer(const VkContext& ctx, VkDeviceSize size,
                       VkBufferUsageFlags usage, VkMemoryPropertyFlags props) {
  GpuBuffer b;
  b.size = size;
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(ctx.device, &bci, nullptr, &b.buf) != VK_SUCCESS) return b;
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(ctx.device, b.buf, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = findMemoryType(ctx, req.memoryTypeBits, props);
  if (mai.memoryTypeIndex == UINT32_MAX ||
      vkAllocateMemory(ctx.device, &mai, nullptr, &b.mem) != VK_SUCCESS) {
    vkDestroyBuffer(ctx.device, b.buf, nullptr);
    b.buf = VK_NULL_HANDLE;
    return b;
  }
  vkBindBufferMemory(ctx.device, b.buf, b.mem, 0);
  return b;
}

void destroyBuffer(const VkContext& ctx, GpuBuffer& b) {
  if (b.buf) vkDestroyBuffer(ctx.device, b.buf, nullptr);
  if (b.mem) vkFreeMemory(ctx.device, b.mem, nullptr);
  b = GpuBuffer{};
}

GpuImage createImage2D(const VkContext& ctx, uint32_t w, uint32_t h,
                       VkFormat format, VkImageUsageFlags usage,
                       VkImageAspectFlags aspect) {
  GpuImage im;
  im.format = format;
  im.width = w;
  im.height = h;
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = format;
  ici.extent = {w, h, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = usage;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(ctx.device, &ici, nullptr, &im.img) != VK_SUCCESS) return im;
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(ctx.device, im.img, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = findMemoryType(ctx, req.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX ||
      vkAllocateMemory(ctx.device, &mai, nullptr, &im.mem) != VK_SUCCESS) {
    vkDestroyImage(ctx.device, im.img, nullptr);
    im.img = VK_NULL_HANDLE;
    return im;
  }
  vkBindImageMemory(ctx.device, im.img, im.mem, 0);
  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = im.img;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = format;
  vci.subresourceRange = {aspect, 0, 1, 0, 1};
  if (vkCreateImageView(ctx.device, &vci, nullptr, &im.view) != VK_SUCCESS) {
    destroyImage(ctx, im);
  }
  return im;
}

void destroyImage(const VkContext& ctx, GpuImage& im) {
  if (im.view) vkDestroyImageView(ctx.device, im.view, nullptr);
  if (im.img) vkDestroyImage(ctx.device, im.img, nullptr);
  if (im.mem) vkFreeMemory(ctx.device, im.mem, nullptr);
  im = GpuImage{};
}

VkCommandBuffer beginOneShot(const VkContext& ctx) {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = ctx.cmdPool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(ctx.device, &ai, &cmd);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  return cmd;
}

void endOneShot(const VkContext& ctx, VkCommandBuffer cmd) {
  vkEndCommandBuffer(cmd);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(ctx.queue, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(ctx.queue);
  vkFreeCommandBuffers(ctx.device, ctx.cmdPool, 1, &cmd);
}

VkShaderModule loadShader(const VkContext& ctx, const std::string& spvPath) {
  FILE* f = std::fopen(spvPath.c_str(), "rb");
  if (!f) return VK_NULL_HANDLE;
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint32_t> code(size_t(size) / 4);
  size_t got = std::fread(code.data(), 1, size_t(size), f);
  std::fclose(f);
  if (got != size_t(size)) return VK_NULL_HANDLE;
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = size_t(size);
  ci.pCode = code.data();
  VkShaderModule mod = VK_NULL_HANDLE;
  vkCreateShaderModule(ctx.device, &ci, nullptr, &mod);
  return mod;
}

void transitionImage(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect,
                     VkImageLayout oldL, VkImageLayout newL) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = oldL;
  b.newLayout = newL;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = img;
  b.subresourceRange = {aspect, 0, 1, 0, 1};
  VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  if (newL == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    b.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  } else if (newL == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    b.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  } else if (newL == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  } else if (newL == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  }
  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

bool readbackImageRgba(const VkContext& ctx, GpuImage& im,
                       std::vector<uint8_t>& outRgba) {
  VkDeviceSize size = VkDeviceSize(im.width) * im.height * 4;
  GpuBuffer staging = createBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (staging.buf == VK_NULL_HANDLE) return false;

  VkCommandBuffer cmd = beginOneShot(ctx);
  transitionImage(cmd, im.img, VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {im.width, im.height, 1};
  vkCmdCopyImageToBuffer(cmd, im.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         staging.buf, 1, &copy);
  endOneShot(ctx, cmd);

  void* mapped = nullptr;
  if (vkMapMemory(ctx.device, staging.mem, 0, size, 0, &mapped) != VK_SUCCESS) {
    destroyBuffer(ctx, staging);
    return false;
  }
  outRgba.resize(size_t(size));
  std::memcpy(outRgba.data(), mapped, size_t(size));
  vkUnmapMemory(ctx.device, staging.mem);
  destroyBuffer(ctx, staging);
  return true;
}
