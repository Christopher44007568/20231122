#include "renderer.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace rpsc {

static const char* layoutName(uint32_t l) {
  switch (VkImageLayout(l)) {
    case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
    case VK_IMAGE_LAYOUT_GENERAL: return "GENERAL";
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return "COLOR_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return "TRANSFER_SRC_OPTIMAL";
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return "TRANSFER_DST_OPTIMAL";
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return "PRESENT_SRC";
    default: return "OTHER";
  }
}

struct Renderer::Impl {
  GLFWwindow* window = nullptr;
  bool glfwUp = false;
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
  VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
  VkImage colorImage = VK_NULL_HANDLE;
  VkDeviceMemory colorMem = VK_NULL_HANDLE;
  VkImageView colorView = VK_NULL_HANDLE;
  VkFramebuffer fb = VK_NULL_HANDLE;
  VkBuffer vtxBuf = VK_NULL_HANDLE;
  VkDeviceMemory vtxMem = VK_NULL_HANDLE;
  VkBuffer readBuf = VK_NULL_HANDLE;
  VkDeviceMemory readMem = VK_NULL_HANDLE;
  VkCommandPool cmdPool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkImageLayout colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  // Pipeline state cache: state hash -> VkPipeline.
  std::unordered_map<uint64_t, VkPipeline> cache;
  std::map<uint32_t, PipelineDesc> descs;  // id -> state (for snapshots)
};

static uint64_t stateKey(const PipelineDesc& d) {
  return fnv1a64(&d, sizeof(d));
}

static bool findMemType(VkPhysicalDevice phys, uint32_t typeBits,
                        VkMemoryPropertyFlags props, uint32_t* out) {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(phys, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags & props) == props) {
      *out = i;
      return true;
    }
  }
  return false;
}

static VkShaderModule loadShader(VkDevice dev, const char* path) {
  FILE* f = std::fopen(path, "rb");
  if (!f) return VK_NULL_HANDLE;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint32_t> code(size_t(n) / 4);
  bool ok = std::fread(code.data(), 1, size_t(n), f) == size_t(n);
  std::fclose(f);
  if (!ok) return VK_NULL_HANDLE;
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = size_t(n);
  ci.pCode = code.data();
  VkShaderModule m = VK_NULL_HANDLE;
  vkCreateShaderModule(dev, &ci, nullptr, &m);
  return m;
}

Renderer::~Renderer() { shutdown(); }

bool Renderer::init(std::string* err) {
  impl_ = new Impl();
  // GLFW windowing context (hidden; rendering stays offscreen).
  if (glfwInit()) {
    impl_->glfwUp = true;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    impl_->window = glfwCreateWindow(64, 64, "rpsc", nullptr, nullptr);
  }

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "rpsc";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  if (vkCreateInstance(&ici, nullptr, &impl_->instance) != VK_SUCCESS) {
    *err = "vkCreateInstance failed";
    return false;
  }
  uint32_t nphys = 0;
  vkEnumeratePhysicalDevices(impl_->instance, &nphys, nullptr);
  if (nphys == 0) {
    *err = "no Vulkan physical device";
    return false;
  }
  std::vector<VkPhysicalDevice> devs(nphys);
  vkEnumeratePhysicalDevices(impl_->instance, &nphys, devs.data());
  impl_->phys = devs[0];
  uint32_t nq = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(impl_->phys, &nq, nullptr);
  std::vector<VkQueueFamilyProperties> qprops(nq);
  vkGetPhysicalDeviceQueueFamilyProperties(impl_->phys, &nq, qprops.data());
  bool found = false;
  for (uint32_t i = 0; i < nq; ++i) {
    if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      impl_->queueFamily = i;
      found = true;
      break;
    }
  }
  if (!found) {
    *err = "no graphics queue family";
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
  if (vkCreateDevice(impl_->phys, &dci, nullptr, &impl_->device) != VK_SUCCESS) {
    *err = "vkCreateDevice failed";
    return false;
  }
  vkGetDeviceQueue(impl_->device, impl_->queueFamily, 0, &impl_->queue);
  return true;
}

static bool createDeviceResources(Renderer::Impl* im, std::string* err) {
  VkAttachmentDescription att{};
  att.format = VK_FORMAT_R8G8B8A8_UNORM;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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
  if (vkCreateRenderPass(im->device, &rpci, nullptr, &im->renderPass) != VK_SUCCESS) {
    *err = "vkCreateRenderPass failed";
    return false;
  }

  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {kFbWidth, kFbHeight, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(im->device, &ici, nullptr, &im->colorImage) != VK_SUCCESS) {
    *err = "vkCreateImage failed";
    return false;
  }
  VkMemoryRequirements mr;
  vkGetImageMemoryRequirements(im->device, im->colorImage, &mr);
  uint32_t mt = 0;
  if (!findMemType(im->phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mt)) {
    *err = "no device-local memory";
    return false;
  }
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = mr.size;
  mai.memoryTypeIndex = mt;
  vkAllocateMemory(im->device, &mai, nullptr, &im->colorMem);
  vkBindImageMemory(im->device, im->colorImage, im->colorMem, 0);

  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = im->colorImage;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = VK_FORMAT_R8G8B8A8_UNORM;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCreateImageView(im->device, &vci, nullptr, &im->colorView);

  VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fci.renderPass = im->renderPass;
  fci.attachmentCount = 1;
  fci.pAttachments = &im->colorView;
  fci.width = kFbWidth;
  fci.height = kFbHeight;
  fci.layers = 1;
  vkCreateFramebuffer(im->device, &fci, nullptr, &im->fb);
  return true;
}

static bool createBuffer(Renderer::Impl* im, VkDeviceSize size,
                         VkBufferUsageFlags usage, VkBuffer* buf,
                         VkDeviceMemory* mem, std::string* err) {
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(im->device, &bci, nullptr, buf) != VK_SUCCESS) {
    *err = "vkCreateBuffer failed";
    return false;
  }
  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(im->device, *buf, &mr);
  uint32_t mt = 0;
  VkMemoryPropertyFlags want =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  if (!findMemType(im->phys, mr.memoryTypeBits, want, &mt)) {
    *err = "no host-visible memory";
    return false;
  }
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = mr.size;
  mai.memoryTypeIndex = mt;
  vkAllocateMemory(im->device, &mai, nullptr, mem);
  vkBindBufferMemory(im->device, *buf, *mem, 0);
  return true;
}

// Six triangles at distinct NDC positions (18 vertices).
static const float kVertices[18][2] = {
    {-0.9f, -0.9f}, {-0.5f, -0.9f}, {-0.7f, -0.4f},
    {-0.3f, -0.9f}, { 0.1f, -0.9f}, {-0.1f, -0.4f},
    { 0.3f, -0.9f}, { 0.7f, -0.9f}, { 0.5f, -0.4f},
    {-0.9f,  0.1f}, {-0.5f,  0.1f}, {-0.7f,  0.6f},
    {-0.3f,  0.1f}, { 0.1f,  0.1f}, {-0.1f,  0.6f},
    { 0.3f,  0.1f}, { 0.7f,  0.1f}, { 0.5f,  0.6f},
};

static bool createGeometryAndShaders(Renderer::Impl* im, std::string* err) {
  if (!createBuffer(im, sizeof(kVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    &im->vtxBuf, &im->vtxMem, err))
    return false;
  void* p = nullptr;
  vkMapMemory(im->device, im->vtxMem, 0, sizeof(kVertices), 0, &p);
  std::memcpy(p, kVertices, sizeof(kVertices));
  vkUnmapMemory(im->device, im->vtxMem);

  if (!createBuffer(im, VkDeviceSize(kFbWidth) * kFbHeight * 4,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT, &im->readBuf, &im->readMem,
                    err))
    return false;

  std::string vsPath = std::string(RPSC_SHADER_DIR) + "/tri.vert.spv";
  std::string fsPath = std::string(RPSC_SHADER_DIR) + "/tri.frag.spv";
  im->vs = loadShader(im->device, vsPath.c_str());
  im->fs = loadShader(im->device, fsPath.c_str());
  if (!im->vs || !im->fs) {
    *err = "failed to load shaders from " + std::string(RPSC_SHADER_DIR);
    return false;
  }
  VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
  VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  lci.pushConstantRangeCount = 1;
  lci.pPushConstantRanges = &pcr;
  vkCreatePipelineLayout(im->device, &lci, nullptr, &im->pipeLayout);

  VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpci.queueFamilyIndex = im->queueFamily;
  cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  vkCreateCommandPool(im->device, &cpci, nullptr, &im->cmdPool);
  VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cbai.commandPool = im->cmdPool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  vkAllocateCommandBuffers(im->device, &cbai, &im->cmd);
  return true;
}

static VkPipeline buildPipeline(Renderer::Impl* im, const PipelineDesc& d) {
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = im->vs;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = im->fs;
  stages[1].pName = "main";

  VkVertexInputBindingDescription bind{0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription attr{0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
  VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vin.vertexBindingDescriptionCount = 1;
  vin.pVertexBindingDescriptions = &bind;
  vin.vertexAttributeDescriptionCount = 1;
  vin.pVertexAttributeDescriptions = &attr;

  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkViewport vp{0, 0, float(d.viewportW), float(d.viewportH), 0.0f, 1.0f};
  VkRect2D sc{{0, 0}, {d.viewportW, d.viewportH}};
  VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vps.viewportCount = 1;
  vps.pViewports = &vp;
  vps.scissorCount = 1;
  vps.pScissors = &sc;

  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VkCullModeFlags(d.cullMode);
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = VkColorComponentFlags(d.colorWriteMask);
  cba.blendEnable = d.blendEnable ? VK_TRUE : VK_FALSE;
  cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  cba.colorBlendOp = VK_BLEND_OP_ADD;
  cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  cba.alphaBlendOp = VK_BLEND_OP_ADD;
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;

  VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gpci.stageCount = 2;
  gpci.pStages = stages;
  gpci.pVertexInputState = &vin;
  gpci.pInputAssemblyState = &ia;
  gpci.pViewportState = &vps;
  gpci.pRasterizationState = &rs;
  gpci.pMultisampleState = &ms;
  gpci.pColorBlendState = &cb;
  gpci.layout = im->pipeLayout;
  gpci.renderPass = im->renderPass;
  VkPipeline pipe = VK_NULL_HANDLE;
  vkCreateGraphicsPipelines(im->device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipe);
  return pipe;
}

static void cmdImageBarrier(VkCommandBuffer cmd, VkImage image,
                            VkImageLayout oldL, VkImageLayout newL) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = oldL;
  b.newLayout = newL;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &b);
}

static void submitAndWait(Renderer::Impl* im) {
  vkEndCommandBuffer(im->cmd);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &im->cmd;
  vkQueueSubmit(im->queue, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(im->queue);
}

// Reads the color image back into `img`. Restores the previous layout.
static void readback(Renderer::Impl* im, Image* img) {
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkResetCommandBuffer(im->cmd, 0);
  vkBeginCommandBuffer(im->cmd, &bi);
  cmdImageBarrier(im->cmd, im->colorImage, im->colorLayout,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {kFbWidth, kFbHeight, 1};
  vkCmdCopyImageToBuffer(im->cmd, im->colorImage,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, im->readBuf, 1,
                         &copy);
  cmdImageBarrier(im->cmd, im->colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  im->colorLayout);
  submitAndWait(im);
  img->width = kFbWidth;
  img->height = kFbHeight;
  img->rgba.resize(size_t(kFbWidth) * kFbHeight * 4);
  void* p = nullptr;
  vkMapMemory(im->device, im->readMem, 0, img->rgba.size(), 0, &p);
  std::memcpy(img->rgba.data(), p, img->rgba.size());
  vkUnmapMemory(im->device, im->readMem);
}

bool Renderer::execute(const Trace& trace, const std::string& frameDir,
                       ReplayResult* out, std::string* err) {
  Impl* im = impl_;
  out->glfwAvailable = im->glfwUp && im->window != nullptr;
  if (!im->renderPass && !createDeviceResources(im, err)) return false;
  if (!im->vs && !createGeometryAndShaders(im, err)) return false;

  // Reset per-run state so every execution starts from the same point:
  // empty pipeline cache, color image back to UNDEFINED.
  for (auto& kv : im->cache) vkDestroyPipeline(im->device, kv.second, nullptr);
  im->cache.clear();
  im->descs.clear();
  if (im->colorLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkResetCommandBuffer(im->cmd, 0);
    vkBeginCommandBuffer(im->cmd, &bi);
    cmdImageBarrier(im->cmd, im->colorImage, im->colorLayout,
                    VK_IMAGE_LAYOUT_UNDEFINED);
    submitAndWait(im);
    im->colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  }

  uint32_t boundId = 0;
  VkPipeline boundPipe = VK_NULL_HANDLE;
  bool frameOpen = false;
  bool cleared = false;
  uint64_t lastFbHash = 0, lastStateHash = 0;
  uint32_t drawIndex = 0;

  auto snapshot = [&](size_t seq, OpType t) {
    OpSnapshot s;
    s.seq = seq;
    s.type = t;
    s.pipelineId = boundId;
    auto it = im->descs.find(boundId);
    if (it != im->descs.end()) {
      s.blendEnable = it->second.blendEnable;
      s.cullMode = it->second.cullMode;
      s.colorWriteMask = it->second.colorWriteMask;
      s.viewportW = it->second.viewportW;
      s.viewportH = it->second.viewportH;
    }
    s.colorImageLayout = uint32_t(im->colorLayout);
    uint32_t fields[7] = {s.pipelineId, s.blendEnable, s.cullMode,
                          s.colorWriteMask, s.viewportW, s.viewportH,
                          s.colorImageLayout};
    s.stateHash = fnv1a64(fields, sizeof(fields));
    s.layoutError = false;
    return s;
  };

  for (size_t i = 0; i < trace.records.size(); ++i) {
    const Record& r = trace.records[i];
    switch (r.type) {
      case OpType::CreatePipeline: {
        PipelineDesc d;
        d.id = r.w[0];
        d.blendEnable = r.w[1];
        d.cullMode = r.w[2];
        d.colorWriteMask = r.w[3];
        d.viewportW = r.w[4];
        d.viewportH = r.w[5];
        uint64_t key = stateKey(d);
        if (im->cache.count(key)) {
          ++out->cacheHits;
        } else {
          ++out->cacheMisses;
          VkPipeline p = buildPipeline(im, d);
          if (!p) {
            *err = "pipeline creation failed";
            return false;
          }
          im->cache[key] = p;
        }
        im->descs[d.id] = d;
        out->ops.push_back(snapshot(i, r.type));
        break;
      }
      case OpType::DestroyPipeline: {
        auto it = im->descs.find(r.w[0]);
        if (it != im->descs.end()) {
          uint64_t key = stateKey(it->second);
          auto cit = im->cache.find(key);
          if (cit != im->cache.end()) {
            vkDestroyPipeline(im->device, cit->second, nullptr);
            im->cache.erase(cit);
          }
          im->descs.erase(it);
        }
        if (boundId == r.w[0]) {
          boundId = 0;
          boundPipe = VK_NULL_HANDLE;
        }
        out->ops.push_back(snapshot(i, r.type));
        break;
      }
      case OpType::BindPipeline: {
        auto it = im->descs.find(r.w[0]);
        if (it == im->descs.end()) {
          OpSnapshot s = snapshot(i, r.type);
          s.note = "bind of unknown pipeline id " + std::to_string(r.w[0]);
          out->ops.push_back(s);
          out->warnings.push_back(s.note);
          break;
        }
        boundId = r.w[0];
        uint64_t key = stateKey(it->second);
        auto cit = im->cache.find(key);
        if (cit == im->cache.end()) {
          ++out->cacheMisses;
          boundPipe = buildPipeline(im, it->second);
          im->cache[key] = boundPipe;
        } else {
          ++out->cacheHits;
          boundPipe = cit->second;
        }
        out->ops.push_back(snapshot(i, r.type));
        break;
      }
      case OpType::BeginFrame:
        frameOpen = true;
        cleared = false;
        out->ops.push_back(snapshot(i, r.type));
        break;
      case OpType::EndFrame:
        frameOpen = false;
        out->ops.push_back(snapshot(i, r.type));
        break;
      case OpType::Barrier: {
        if (r.w[0] == 0) {  // color image
          VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
          vkResetCommandBuffer(im->cmd, 0);
          vkBeginCommandBuffer(im->cmd, &bi);
          cmdImageBarrier(im->cmd, im->colorImage, im->colorLayout,
                          VkImageLayout(r.w[2]));
          submitAndWait(im);
          OpSnapshot s = snapshot(i, r.type);
          if (uint32_t(im->colorLayout) != r.w[1]) {
            s.note = std::string("declared oldLayout ") + layoutName(r.w[1]) +
                     " != actual " + layoutName(uint32_t(im->colorLayout));
            out->warnings.push_back(s.note);
          }
          im->colorLayout = VkImageLayout(r.w[2]);
          s.colorImageLayout = r.w[2];
          uint32_t fields[7] = {s.pipelineId, s.blendEnable, s.cullMode,
                                s.colorWriteMask, s.viewportW, s.viewportH,
                                s.colorImageLayout};
          s.stateHash = fnv1a64(fields, sizeof(fields));
          out->ops.push_back(s);
        }
        break;
      }
      case OpType::Draw: {
        OpSnapshot s = snapshot(i, r.type);
        if (!frameOpen) s.note = "draw outside frame";
        if (!boundPipe) {
          s.note += (s.note.empty() ? "" : "; ");
          s.note += "no pipeline bound, draw skipped";
          out->warnings.push_back(s.note);
          out->ops.push_back(s);
          break;
        }
        if (im->colorLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
          s.layoutError = true;
          s.note += (s.note.empty() ? "" : "; ");
          s.note += std::string("draw in layout ") +
                    layoutName(uint32_t(im->colorLayout)) + ", auto-fixed";
          VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
          vkResetCommandBuffer(im->cmd, 0);
          vkBeginCommandBuffer(im->cmd, &bi);
          cmdImageBarrier(im->cmd, im->colorImage, im->colorLayout,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
          submitAndWait(im);
          im->colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkResetCommandBuffer(im->cmd, 0);
        vkBeginCommandBuffer(im->cmd, &bi);
        VkClearValue clear{};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = im->renderPass;
        rp.framebuffer = im->fb;
        rp.renderArea = {{0, 0}, {kFbWidth, kFbHeight}};
        // loadOp is LOAD in the render pass; the first draw of a frame
        // clears via vkCmdClearAttachments, later draws accumulate.
        vkCmdBeginRenderPass(im->cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        if (!cleared) {
          VkClearAttachment ca{VK_IMAGE_ASPECT_COLOR_BIT, 0, clear};
          VkClearRect rect{{{0, 0}, {kFbWidth, kFbHeight}}, 0, 1};
          vkCmdClearAttachments(im->cmd, 1, &ca, 1, &rect);
          cleared = true;
        }
        vkCmdBindPipeline(im->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, boundPipe);
        float color[4] = {float(r.w[3] & 0xFF) / 255.0f,
                          float((r.w[3] >> 8) & 0xFF) / 255.0f,
                          float((r.w[3] >> 16) & 0xFF) / 255.0f,
                          float((r.w[3] >> 24) & 0xFF) / 255.0f};
        vkCmdPushConstants(im->cmd, im->pipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 16, color);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(im->cmd, 0, 1, &im->vtxBuf, &off);
        uint32_t instances = r.w[2] ? r.w[2] : 1;
        vkCmdDraw(im->cmd, r.w[0], instances, r.w[1], 0);
        vkCmdEndRenderPass(im->cmd);
        submitAndWait(im);

        Image img;
        readback(im, &img);
        s.fbHash = img.hash();
        s.hasFbHash = true;
        lastFbHash = s.fbHash;
        lastStateHash = s.stateHash;
        ++out->drawsExecuted;
        if (!frameDir.empty()) {
          char name[64];
          std::snprintf(name, sizeof(name), "/draw_%02u.ppm", drawIndex);
          std::string ioErr;
          savePpm(frameDir + name, img, &ioErr);
        }
        ++drawIndex;
        out->ops.push_back(s);
        break;
      }
      case OpType::Checkpoint: {
        OpSnapshot s = snapshot(i, r.type);
        uint64_t wantFb = r.u64(0);
        uint64_t wantState = r.u64(1);
        if (wantFb != lastFbHash || wantState != lastStateHash) {
          char msg[160];
          std::snprintf(msg, sizeof(msg),
                        "checkpoint at seq %zu: fb %016llx (want %016llx), "
                        "state %016llx (want %016llx)",
                        i, (unsigned long long)lastFbHash,
                        (unsigned long long)wantFb,
                        (unsigned long long)lastStateHash,
                        (unsigned long long)wantState);
          out->checkpointMismatches.push_back(msg);
          s.note = "checkpoint mismatch";
        }
        s.fbHash = lastFbHash;
        s.hasFbHash = true;
        out->ops.push_back(s);
        break;
      }
    }
  }
  return true;
}

void Renderer::shutdown() {
  if (!impl_) return;
  Impl* im = impl_;
  if (im->device) {
    vkDeviceWaitIdle(im->device);
    for (auto& kv : im->cache) vkDestroyPipeline(im->device, kv.second, nullptr);
    im->cache.clear();
    if (im->cmdPool) vkDestroyCommandPool(im->device, im->cmdPool, nullptr);
    if (im->pipeLayout) vkDestroyPipelineLayout(im->device, im->pipeLayout, nullptr);
    if (im->vs) vkDestroyShaderModule(im->device, im->vs, nullptr);
    if (im->fs) vkDestroyShaderModule(im->device, im->fs, nullptr);
    if (im->fb) vkDestroyFramebuffer(im->device, im->fb, nullptr);
    if (im->colorView) vkDestroyImageView(im->device, im->colorView, nullptr);
    if (im->colorImage) vkDestroyImage(im->device, im->colorImage, nullptr);
    if (im->colorMem) vkFreeMemory(im->device, im->colorMem, nullptr);
    if (im->vtxBuf) vkDestroyBuffer(im->device, im->vtxBuf, nullptr);
    if (im->vtxMem) vkFreeMemory(im->device, im->vtxMem, nullptr);
    if (im->readBuf) vkDestroyBuffer(im->device, im->readBuf, nullptr);
    if (im->readMem) vkFreeMemory(im->device, im->readMem, nullptr);
    if (im->renderPass) vkDestroyRenderPass(im->device, im->renderPass, nullptr);
    vkDestroyDevice(im->device, nullptr);
  }
  if (im->instance) vkDestroyInstance(im->instance, nullptr);
  if (im->window) glfwDestroyWindow(im->window);
  if (im->glfwUp) glfwTerminate();
  delete im;
  impl_ = nullptr;
}

}  // namespace rpsc
