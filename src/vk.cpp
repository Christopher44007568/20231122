#include "vk.h"
#include <stdexcept>
#include <cstring>

#define VK_CHECK(x)                                                     \
  do {                                                                  \
    VkResult r_ = (x);                                                  \
    if (r_ != VK_SUCCESS)                                               \
      throw std::runtime_error(std::string("vk error ") +               \
                               std::to_string(r_) + " at " #x);         \
  } while (0)

void Gpu::init() {
  VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  ai.pApplicationName = "reflection-probe";
  ai.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &ai;
  VK_CHECK(vkCreateInstance(&ici, nullptr, &instance));

  uint32_t n = 0;
  VK_CHECK(vkEnumeratePhysicalDevices(instance, &n, nullptr));
  if (!n) throw std::runtime_error("no Vulkan physical device");
  std::vector<VkPhysicalDevice> devs(n);
  vkEnumeratePhysicalDevices(instance, &n, devs.data());
  phys = devs[0];

  uint32_t qn = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, nullptr);
  std::vector<VkQueueFamilyProperties> qp(qn);
  vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, qp.data());
  for (uint32_t i = 0; i < qn; ++i)
    if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qfam = i; break; }

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = qfam;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  VK_CHECK(vkCreateDevice(phys, &dci, nullptr, &dev));
  vkGetDeviceQueue(dev, qfam, 0, &queue);

  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.queueFamilyIndex = qfam;
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VK_CHECK(vkCreateCommandPool(dev, &pci, nullptr, &pool));
}

void Gpu::destroy() {
  if (dev) {
    vkDeviceWaitIdle(dev);
    vkDestroyCommandPool(dev, pool, nullptr);
    vkDestroyDevice(dev, nullptr);
  }
  if (instance) vkDestroyInstance(instance, nullptr);
}

VkCommandBuffer Gpu::beginCmd() {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = pool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd;
  VK_CHECK(vkAllocateCommandBuffers(dev, &ai, &cmd));
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  return cmd;
}

void Gpu::submitWait(VkCommandBuffer cmd) {
  vkEndCommandBuffer(cmd);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  VK_CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
  VK_CHECK(vkQueueWaitIdle(queue));
  vkFreeCommandBuffers(dev, pool, 1, &cmd);
}

uint32_t Gpu::memType(uint32_t bits, VkMemoryPropertyFlags props) {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(phys, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
      return i;
  throw std::runtime_error("no suitable memory type");
}

GpuImage createImage(Gpu& g, uint32_t w, uint32_t h, VkImageUsageFlags usage) {
  GpuImage im;
  im.w = w; im.h = h;
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {w, h, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = usage;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VK_CHECK(vkCreateImage(g.dev, &ici, nullptr, &im.img));
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(g.dev, im.img, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = g.memType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VK_CHECK(vkAllocateMemory(g.dev, &mai, nullptr, &im.mem));
  vkBindImageMemory(g.dev, im.img, im.mem, 0);
  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = im.img;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = ici.format;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VK_CHECK(vkCreateImageView(g.dev, &vci, nullptr, &im.view));
  return im;
}

void destroyImage(Gpu& g, GpuImage& im) {
  vkDestroyImageView(g.dev, im.view, nullptr);
  vkDestroyImage(g.dev, im.img, nullptr);
  vkFreeMemory(g.dev, im.mem, nullptr);
  im = {};
}

void transition(Gpu& g, GpuImage& im, VkImageLayout to) {
  VkCommandBuffer cmd = g.beginCmd();
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = im.layout;
  b.newLayout = to;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = im.img;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkPipelineStageFlags srcStage, dstStage;
  auto accessFor = [](VkImageLayout l, VkAccessFlags& a, VkPipelineStageFlags& s) {
    switch (l) {
      case VK_IMAGE_LAYOUT_UNDEFINED: a = 0; s = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; break;
      case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        a = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; s = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; break;
      case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        a = VK_ACCESS_SHADER_READ_BIT; s = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; break;
      case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        a = VK_ACCESS_TRANSFER_READ_BIT; s = VK_PIPELINE_STAGE_TRANSFER_BIT; break;
      default: a = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; s = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; break;
    }
  };
  accessFor(im.layout, b.srcAccessMask, srcStage);
  accessFor(to, b.dstAccessMask, dstStage);
  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
  g.submitWait(cmd);
  im.layout = to;
}

Image readback(Gpu& g, GpuImage& im, VkImageLayout restore) {
  transition(g, im, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  GpuBuffer staging = createBuffer(g, VkDeviceSize(im.w) * im.h * 4,
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   true);
  VkCommandBuffer cmd = g.beginCmd();
  VkBufferImageCopy c{};
  c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  c.imageExtent = {im.w, im.h, 1};
  vkCmdCopyImageToBuffer(cmd, im.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         staging.buf, 1, &c);
  g.submitWait(cmd);
  Image out;
  out.width = im.w; out.height = im.h;
  out.rgba.resize(size_t(im.w) * im.h * 4);
  memcpy(out.rgba.data(), staging.mapped, out.rgba.size());
  destroyBuffer(g, staging);
  transition(g, im, restore);
  return out;
}

GpuBuffer createBuffer(Gpu& g, VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags props, bool map) {
  GpuBuffer b;
  b.size = size;
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VK_CHECK(vkCreateBuffer(g.dev, &bci, nullptr, &b.buf));
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(g.dev, b.buf, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = g.memType(req.memoryTypeBits, props);
  VK_CHECK(vkAllocateMemory(g.dev, &mai, nullptr, &b.mem));
  vkBindBufferMemory(g.dev, b.buf, b.mem, 0);
  if (map) vkMapMemory(g.dev, b.mem, 0, size, 0, &b.mapped);
  return b;
}

void destroyBuffer(Gpu& g, GpuBuffer& b) {
  if (b.mapped) vkUnmapMemory(g.dev, b.mem);
  vkDestroyBuffer(g.dev, b.buf, nullptr);
  vkFreeMemory(g.dev, b.mem, nullptr);
  b = {};
}
