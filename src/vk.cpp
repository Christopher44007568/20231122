#include "vk.h"
#include "evidence.h"
#include "util.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <cstring>
#include <vector>

namespace tcpv {

struct VkEngine::Impl {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VkCommandPool cmdPool = VK_NULL_HANDLE;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorSetLayout descLayout = VK_NULL_HANDLE;
  VkDescriptorPool descPool = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
};

namespace {

std::string vkErr(VkResult r) {
  return "VkResult=" + std::to_string(int(r));
}

const char* layoutName(VkImageLayout l) {
  switch (l) {
    case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return "TRANSFER_DST_OPTIMAL";
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      return "SHADER_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return "TRANSFER_SRC_OPTIMAL";
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      return "COLOR_ATTACHMENT_OPTIMAL";
    default: return "OTHER";
  }
}

} // namespace

bool VkEngine::init(bool windowed, EventLog& log) {
  log_ = &log;
  glfwSetErrorCallback([](int code, const char* msg) {
    (void)code;
    (void)msg;
  });
  if (!glfwInit()) {
    error_ = "glfwInit failed";
    return false;
  }
  if (!glfwVulkanSupported()) {
    error_ = "GLFW reports Vulkan not supported";
    return false;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  if (windowed) {
    window_ = glfwCreateWindow(640, 480, "tcpv preview", nullptr, nullptr);
    // Window creation may fail on headless/null platform; the run continues
    // offscreen regardless.
  }

  impl_ = new Impl();

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "tcpv";
  app.apiVersion = VK_API_VERSION_1_1;

  uint32_t extCount = 0;
  const char** exts = nullptr;
  if (window_) exts = glfwGetRequiredInstanceExtensions(&extCount);

  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = extCount;
  ici.ppEnabledExtensionNames = exts;
  VkResult r = vkCreateInstance(&ici, nullptr, &impl_->instance);
  if (r != VK_SUCCESS) {
    error_ = "vkCreateInstance: " + vkErr(r);
    return false;
  }

  uint32_t n = 0;
  vkEnumeratePhysicalDevices(impl_->instance, &n, nullptr);
  if (n == 0) {
    error_ = "no Vulkan physical devices";
    return false;
  }
  std::vector<VkPhysicalDevice> devs(n);
  vkEnumeratePhysicalDevices(impl_->instance, &n, devs.data());
  impl_->phys = devs[0];
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(impl_->phys, &props);
  deviceName_ = props.deviceName;

  uint32_t qn = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(impl_->phys, &qn, nullptr);
  std::vector<VkQueueFamilyProperties> qprops(qn);
  vkGetPhysicalDeviceQueueFamilyProperties(impl_->phys, &qn, qprops.data());
  bool found = false;
  for (uint32_t i = 0; i < qn; i++)
    if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      impl_->queueFamily = i;
      found = true;
      break;
    }
  if (!found) {
    error_ = "no graphics queue family";
    return false;
  }

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = impl_->queueFamily;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  r = vkCreateDevice(impl_->phys, &dci, nullptr, &impl_->device);
  if (r != VK_SUCCESS) {
    error_ = "vkCreateDevice: " + vkErr(r);
    return false;
  }
  vkGetDeviceQueue(impl_->device, impl_->queueFamily, 0, &impl_->queue);

  VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpci.queueFamilyIndex = impl_->queueFamily;
  cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  vkCreateCommandPool(impl_->device, &cpci, nullptr, &impl_->cmdPool);

  device_ = impl_->device;
  log.lifecycle("vulkan_init", std::string("device=") + deviceName_);
  return true;
}

void VkEngine::shutdown() {
  if (impl_ && impl_->device) {
    vkDeviceWaitIdle(impl_->device);
    if (impl_->sampler) vkDestroySampler(impl_->device, impl_->sampler, nullptr);
    if (impl_->pipeline) vkDestroyPipeline(impl_->device, impl_->pipeline, nullptr);
    if (impl_->pipelineLayout)
      vkDestroyPipelineLayout(impl_->device, impl_->pipelineLayout, nullptr);
    if (impl_->descPool)
      vkDestroyDescriptorPool(impl_->device, impl_->descPool, nullptr);
    if (impl_->descLayout)
      vkDestroyDescriptorSetLayout(impl_->device, impl_->descLayout, nullptr);
    if (impl_->renderPass)
      vkDestroyRenderPass(impl_->device, impl_->renderPass, nullptr);
    if (impl_->cmdPool)
      vkDestroyCommandPool(impl_->device, impl_->cmdPool, nullptr);
    vkDestroyDevice(impl_->device, nullptr);
  }
  if (impl_ && impl_->instance) vkDestroyInstance(impl_->instance, nullptr);
  delete impl_;
  impl_ = nullptr;
  device_ = nullptr;
  if (window_) glfwDestroyWindow(window_);
  window_ = nullptr;
  glfwTerminate();
}

bool VkEngine::cancelRequested() {
  glfwPollEvents();
  if (!window_) return false;
  return glfwWindowShouldClose(window_) ||
         glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
}

double VkEngine::timeSeconds() const { return glfwGetTime(); }

namespace {

uint32_t findMemType(VkPhysicalDevice phys, uint32_t bits,
                     VkMemoryPropertyFlags flags) {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(phys, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
    if ((bits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags & flags) == flags)
      return i;
  return UINT32_MAX;
}

struct GpuBuffer {
  VkBuffer buf = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
};

GpuBuffer makeBuffer(VkDevice dev, VkPhysicalDevice phys, VkDeviceSize size,
                     VkBufferUsageFlags usage, VkMemoryPropertyFlags props) {
  GpuBuffer b;
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(dev, &bci, nullptr, &b.buf) != VK_SUCCESS) return b;
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(dev, b.buf, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = findMemType(phys, req.memoryTypeBits, props);
  if (mai.memoryTypeIndex == UINT32_MAX ||
      vkAllocateMemory(dev, &mai, nullptr, &b.mem) != VK_SUCCESS) {
    vkDestroyBuffer(dev, b.buf, nullptr);
    b.buf = VK_NULL_HANDLE;
    return b;
  }
  vkBindBufferMemory(dev, b.buf, b.mem, 0);
  return b;
}

void freeBuffer(VkDevice dev, GpuBuffer& b) {
  if (b.buf) vkDestroyBuffer(dev, b.buf, nullptr);
  if (b.mem) vkFreeMemory(dev, b.mem, nullptr);
  b = {};
}

VkCommandBuffer beginOnce(VkDevice dev, VkCommandPool pool) {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = pool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cb;
  vkAllocateCommandBuffers(dev, &ai, &cb);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cb, &bi);
  return cb;
}

void endOnce(VkDevice dev, VkCommandPool pool, VkQueue q, VkCommandBuffer cb) {
  vkEndCommandBuffer(cb);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cb;
  vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(q);
  vkFreeCommandBuffers(dev, pool, 1, &cb);
}

} // namespace

// Record an image layout transition both to Vulkan and to the evidence log.
static void transition(VkEngine& self, VkCommandBuffer cb, VkImage img,
                       const std::string& resourceId, VkImageLayout from,
                       VkImageLayout to, VkPipelineStageFlags srcStage,
                       VkPipelineStageFlags dstStage, VkAccessFlags srcAccess,
                       VkAccessFlags dstAccess, const std::string& drawCallId,
                       EventLog* log) {
  (void)self;
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = from;
  b.newLayout = to;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = img;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  b.srcAccessMask = srcAccess;
  b.dstAccessMask = dstAccess;
  vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
  if (log)
    log->resourceState(resourceId, "image_layout", layoutName(from),
                       layoutName(to), drawCallId);
}

namespace {

VkShaderModule loadShader(VkDevice dev, const char* path) {
  std::string code = readFile(path);
  if (code.empty()) return VK_NULL_HANDLE;
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = code.size();
  ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
  VkShaderModule m;
  if (vkCreateShaderModule(dev, &ci, nullptr, &m) != VK_SUCCESS)
    return VK_NULL_HANDLE;
  return m;
}

// Lazily create render pass, descriptor layout/pool, sampler and pipeline.
bool ensurePipeline(VkEngine::Impl& im, std::string& err) {
  if (im.pipeline) return true;
  VkDevice dev = im.device;

  VkAttachmentDescription att{};
  att.format = VK_FORMAT_R8G8B8A8_UNORM;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &ref;
  VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpci.attachmentCount = 1;
  rpci.pAttachments = &att;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &sub;
  if (vkCreateRenderPass(dev, &rpci, nullptr, &im.renderPass) != VK_SUCCESS) {
    err = "vkCreateRenderPass failed";
    return false;
  }

  VkDescriptorSetLayoutBinding bind{};
  bind.binding = 0;
  bind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bind.descriptorCount = 1;
  bind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo dlci{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dlci.bindingCount = 1;
  dlci.pBindings = &bind;
  vkCreateDescriptorSetLayout(dev, &dlci, nullptr, &im.descLayout);

  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64};
  VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpci.maxSets = 64;
  dpci.poolSizeCount = 1;
  dpci.pPoolSizes = &ps;
  vkCreateDescriptorPool(dev, &dpci, nullptr, &im.descPool);

  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = VK_FILTER_NEAREST;
  sci.minFilter = VK_FILTER_NEAREST;
  sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  vkCreateSampler(dev, &sci, nullptr, &im.sampler);

  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &im.descLayout;
  vkCreatePipelineLayout(dev, &plci, nullptr, &im.pipelineLayout);

  std::string dir = TCPV_SHADER_DIR;
  VkShaderModule vs = loadShader(dev, (dir + "/quad.vert.spv").c_str());
  VkShaderModule fs = loadShader(dev, (dir + "/quad.frag.spv").c_str());
  if (!vs || !fs) {
    err = "shader load failed (missing SPIR-V under " + dir + ")";
    return false;
  }
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs;
  stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo vin{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;
  VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dynStates;

  VkGraphicsPipelineCreateInfo gpci{
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gpci.stageCount = 2;
  gpci.pStages = stages;
  gpci.pVertexInputState = &vin;
  gpci.pInputAssemblyState = &ia;
  gpci.pViewportState = &vp;
  gpci.pRasterizationState = &rs;
  gpci.pMultisampleState = &ms;
  gpci.pColorBlendState = &cb;
  gpci.pDynamicState = &dyn;
  gpci.layout = im.pipelineLayout;
  gpci.renderPass = im.renderPass;
  VkResult r = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, nullptr,
                                         &im.pipeline);
  vkDestroyShaderModule(dev, vs, nullptr);
  vkDestroyShaderModule(dev, fs, nullptr);
  if (r != VK_SUCCESS) {
    err = "vkCreateGraphicsPipelines: " + vkErr(r);
    return false;
  }
  return true;
}

} // namespace

GpuTexture VkEngine::uploadTexture(const std::string& resourceId,
                                   const Image& img) {
  GpuTexture t;
  t.resourceId = resourceId;
  VkDevice dev = impl_->device;
  VkDeviceSize size = img.rgba.size();

  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {img.width, img.height, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImage vkimg;
  if (vkCreateImage(dev, &ici, nullptr, &vkimg) != VK_SUCCESS) return t;
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(dev, vkimg, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex =
      findMemType(impl_->phys, req.memoryTypeBits,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VkDeviceMemory mem;
  if (vkAllocateMemory(dev, &mai, nullptr, &mem) != VK_SUCCESS) {
    vkDestroyImage(dev, vkimg, nullptr);
    return t;
  }
  vkBindImageMemory(dev, vkimg, mem, 0);

  GpuBuffer staging =
      makeBuffer(dev, impl_->phys, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!staging.buf) {
    vkFreeMemory(dev, mem, nullptr);
    vkDestroyImage(dev, vkimg, nullptr);
    return t;
  }
  void* mapped = nullptr;
  vkMapMemory(dev, staging.mem, 0, size, 0, &mapped);
  std::memcpy(mapped, img.rgba.data(), size);
  vkUnmapMemory(dev, staging.mem);

  VkCommandBuffer cb = beginOnce(dev, impl_->cmdPool);
  transition(*this, cb, vkimg, resourceId, VK_IMAGE_LAYOUT_UNDEFINED,
             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
             0, VK_ACCESS_TRANSFER_WRITE_BIT, "upload", log_);
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {img.width, img.height, 1};
  vkCmdCopyBufferToImage(cb, staging.buf, vkimg,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  transition(*this, cb, vkimg, resourceId,
             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
             VK_PIPELINE_STAGE_TRANSFER_BIT,
             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
             VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, "upload",
             log_);
  endOnce(dev, impl_->cmdPool, impl_->queue, cb);
  freeBuffer(dev, staging);

  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = vkimg;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = VK_FORMAT_R8G8B8A8_UNORM;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView view;
  if (vkCreateImageView(dev, &vci, nullptr, &view) != VK_SUCCESS) {
    vkFreeMemory(dev, mem, nullptr);
    vkDestroyImage(dev, vkimg, nullptr);
    return t;
  }
  t.image = uint64_t(vkimg);
  t.view = uint64_t(view);
  t.memory = uint64_t(mem);
  return t;
}

void VkEngine::destroyTexture(GpuTexture& t) {
  VkDevice dev = impl_->device;
  if (t.view) vkDestroyImageView(dev, VkImageView(t.view), nullptr);
  if (t.image) vkDestroyImage(dev, VkImage(t.image), nullptr);
  if (t.memory) vkFreeMemory(dev, VkDeviceMemory(t.memory), nullptr);
  t = GpuTexture{};
}

RenderResult VkEngine::renderQuad(const GpuTexture& tex, uint32_t w, uint32_t h,
                                  const std::string& drawCallId) {
  RenderResult rr;
  rr.drawCallId = drawCallId;
  VkDevice dev = impl_->device;
  if (!ensurePipeline(*impl_, rr.error)) return rr;

  // Offscreen target + framebuffer, one per draw call for clean correlation.
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {w, h, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  VkImage target;
  if (vkCreateImage(dev, &ici, nullptr, &target) != VK_SUCCESS) {
    rr.error = "target vkCreateImage failed";
    return rr;
  }
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(dev, target, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = findMemType(impl_->phys, req.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VkDeviceMemory targetMem;
  vkAllocateMemory(dev, &mai, nullptr, &targetMem);
  vkBindImageMemory(dev, target, targetMem, 0);
  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = target;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = VK_FORMAT_R8G8B8A8_UNORM;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView targetView;
  vkCreateImageView(dev, &vci, nullptr, &targetView);

  VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fci.renderPass = impl_->renderPass;
  fci.attachmentCount = 1;
  fci.pAttachments = &targetView;
  fci.width = w;
  fci.height = h;
  fci.layers = 1;
  VkFramebuffer fb;
  vkCreateFramebuffer(dev, &fci, nullptr, &fb);
  rr.framebufferId = "fb/" + drawCallId;
  std::string targetId = "target/" + drawCallId;
  log_->framebuffer(rr.framebufferId, "R8G8B8A8_UNORM", w, h, drawCallId);

  // Descriptor set for this texture.
  VkDescriptorSetAllocateInfo dsai{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dsai.descriptorPool = impl_->descPool;
  dsai.descriptorSetCount = 1;
  dsai.pSetLayouts = &impl_->descLayout;
  VkDescriptorSet dset;
  vkAllocateDescriptorSets(dev, &dsai, &dset);
  VkDescriptorImageInfo dii{};
  dii.sampler = impl_->sampler;
  dii.imageView = VkImageView(tex.view);
  dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  wr.dstSet = dset;
  wr.dstBinding = 0;
  wr.descriptorCount = 1;
  wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  wr.pImageInfo = &dii;
  vkUpdateDescriptorSets(dev, 1, &wr, 0, nullptr);

  VkCommandBuffer cb = beginOnce(dev, impl_->cmdPool);
  VkClearValue clear{};
  VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rbi.renderPass = impl_->renderPass;
  rbi.framebuffer = fb;
  rbi.renderArea = {{0, 0}, {w, h}};
  rbi.clearValueCount = 1;
  rbi.pClearValues = &clear;
  // Render pass performs UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL ->
  // TRANSFER_SRC_OPTIMAL implicitly; record them for the audit trail.
  log_->resourceState(targetId, "image_layout", "UNDEFINED",
                      "COLOR_ATTACHMENT_OPTIMAL", drawCallId);
  vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->pipeline);
  VkViewport viewport{0, 0, float(w), float(h), 0.0f, 1.0f};
  vkCmdSetViewport(cb, 0, 1, &viewport);
  VkRect2D scissor{{0, 0}, {w, h}};
  vkCmdSetScissor(cb, 0, 1, &scissor);
  vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          impl_->pipelineLayout, 0, 1, &dset, 0, nullptr);
  vkCmdDraw(cb, 3, 1, 0, 0);
  log_->drawCall(drawCallId, tex.resourceId, rr.framebufferId, 3,
                 "textured-quad");
  vkCmdEndRenderPass(cb);
  log_->resourceState(targetId, "image_layout", "COLOR_ATTACHMENT_OPTIMAL",
                      "TRANSFER_SRC_OPTIMAL", drawCallId);

  GpuBuffer readback =
      makeBuffer(dev, impl_->phys, VkDeviceSize(w) * h * 4,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {w, h, 1};
  vkCmdCopyImageToBuffer(cb, target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         readback.buf, 1, &region);
  endOnce(dev, impl_->cmdPool, impl_->queue, cb);

  rr.captured = Image(w, h);
  void* mapped = nullptr;
  vkMapMemory(dev, readback.mem, 0, VkDeviceSize(w) * h * 4, 0, &mapped);
  std::memcpy(rr.captured.rgba.data(), mapped, rr.captured.rgba.size());
  vkUnmapMemory(dev, readback.mem);

  freeBuffer(dev, readback);
  vkDestroyFramebuffer(dev, fb, nullptr);
  vkDestroyImageView(dev, targetView, nullptr);
  vkDestroyImage(dev, target, nullptr);
  vkFreeMemory(dev, targetMem, nullptr);
  rr.ok = true;
  return rr;
}

} // namespace tcpv
