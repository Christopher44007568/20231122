#include "renderer.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace {

void vkCheck(VkResult r, const char* what) {
  if (r != VK_SUCCESS)
    throw std::runtime_error(std::string(what) + " failed with VkResult " + std::to_string(r));
}

std::vector<uint32_t> loadSpv(const std::string& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) throw std::runtime_error("cannot open shader: " + path);
  std::vector<char> bytes((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
  if (bytes.size() % 4 != 0) throw std::runtime_error("bad SPIR-V size: " + path);
  std::vector<uint32_t> words(bytes.size() / 4);
  std::memcpy(words.data(), bytes.data(), bytes.size());
  return words;
}

VkShaderModule createModule(VkDevice dev, const std::string& path) {
  auto words = loadSpv(path);
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = words.size() * 4;
  ci.pCode = words.data();
  VkShaderModule m = VK_NULL_HANDLE;
  vkCheck(vkCreateShaderModule(dev, &ci, nullptr, &m), "vkCreateShaderModule");
  return m;
}

}  // namespace

Renderer::Renderer(VkContext& ctx, uint32_t width, uint32_t height)
    : ctx_(ctx), width_(width), height_(height) {
  VkDevice dev = ctx_.device();

  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {width_, height_, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  vkCheck(vkCreateImage(dev, &ici, nullptr, &colorImage_), "vkCreateImage");

  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(dev, colorImage_, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = ctx_.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vkCheck(vkAllocateMemory(dev, &mai, nullptr, &colorMemory_), "vkAllocateMemory(color)");
  vkCheck(vkBindImageMemory(dev, colorImage_, colorMemory_, 0), "vkBindImageMemory");

  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = colorImage_;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = VK_FORMAT_R8G8B8A8_UNORM;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCheck(vkCreateImageView(dev, &vci, nullptr, &colorView_), "vkCreateImageView");

  auto makeRenderPass = [&](VkAttachmentLoadOp loadOp, VkImageLayout initial,
                            VkRenderPass* out) {
    VkAttachmentDescription att{};
    att.format = VK_FORMAT_R8G8B8A8_UNORM;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = loadOp;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = initial;
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
    vkCheck(vkCreateRenderPass(dev, &rpci, nullptr, out), "vkCreateRenderPass");
  };
  makeRenderPass(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, &renderPassClear_);
  makeRenderPass(VK_ATTACHMENT_LOAD_OP_LOAD, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 &renderPassLoad_);

  VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fci.renderPass = renderPassClear_;
  fci.attachmentCount = 1;
  fci.pAttachments = &colorView_;
  fci.width = width_;
  fci.height = height_;
  fci.layers = 1;
  vkCheck(vkCreateFramebuffer(dev, &fci, nullptr, &framebuffer_), "vkCreateFramebuffer");

  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  vkCheck(vkCreatePipelineLayout(dev, &plci, nullptr, &pipelineLayout_), "vkCreatePipelineLayout");

  vertModule_ = createModule(dev, std::string(SHADER_DIR) + "/tri.vert.spv");
  fragModule_ = createModule(dev, std::string(SHADER_DIR) + "/tri.frag.spv");
}

Renderer::~Renderer() {
  VkDevice dev = ctx_.device();
  vkDeviceWaitIdle(dev);
  for (auto& [topo, pipe] : pipelines_) vkDestroyPipeline(dev, pipe, nullptr);
  if (vertModule_) vkDestroyShaderModule(dev, vertModule_, nullptr);
  if (fragModule_) vkDestroyShaderModule(dev, fragModule_, nullptr);
  if (pipelineLayout_) vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr);
  if (framebuffer_) vkDestroyFramebuffer(dev, framebuffer_, nullptr);
  if (renderPassClear_) vkDestroyRenderPass(dev, renderPassClear_, nullptr);
  if (renderPassLoad_) vkDestroyRenderPass(dev, renderPassLoad_, nullptr);
  if (colorView_) vkDestroyImageView(dev, colorView_, nullptr);
  if (colorImage_) vkDestroyImage(dev, colorImage_, nullptr);
  if (colorMemory_) vkFreeMemory(dev, colorMemory_, nullptr);
  if (vertexBuffer_) vkDestroyBuffer(dev, vertexBuffer_, nullptr);
  if (vertexMemory_) vkFreeMemory(dev, vertexMemory_, nullptr);
}

VkPipeline Renderer::pipelineFor(uint32_t topology) {
  auto it = pipelines_.find(topology);
  if (it != pipelines_.end()) return it->second;

  VkDevice dev = ctx_.device();
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertModule_;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragModule_;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{0, sizeof(ptdc::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription attrs[2]{};
  attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ptdc::Vertex, pos)};
  attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ptdc::Vertex, color)};
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &binding;
  vi.vertexAttributeDescriptionCount = 2;
  vi.pVertexAttributeDescriptions = attrs;

  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = static_cast<VkPrimitiveTopology>(topology);

  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blendAtt{};
  blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &blendAtt;

  VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dynStates;

  VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pci.stageCount = 2;
  pci.pStages = stages;
  pci.pVertexInputState = &vi;
  pci.pInputAssemblyState = &ia;
  pci.pViewportState = &vp;
  pci.pRasterizationState = &rs;
  pci.pMultisampleState = &ms;
  pci.pColorBlendState = &cb;
  pci.pDynamicState = &dyn;
  pci.layout = pipelineLayout_;
  pci.renderPass = renderPassClear_;

  VkPipeline pipe = VK_NULL_HANDLE;
  VkResult r = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
  if (r != VK_SUCCESS)
    throw std::runtime_error(std::string("cannot build pipeline for topology ") +
                             ptdc::topologyName(topology) + " (VkResult " + std::to_string(r) + ")");
  pipelines_[topology] = pipe;
  return pipe;
}

void Renderer::uploadVertices(const std::vector<ptdc::Vertex>& verts) {
  VkDevice dev = ctx_.device();
  if (vertexBuffer_) {
    vkDeviceWaitIdle(dev);
    vkDestroyBuffer(dev, vertexBuffer_, nullptr);
    vkFreeMemory(dev, vertexMemory_, nullptr);
    vertexBuffer_ = VK_NULL_HANDLE;
    vertexMemory_ = VK_NULL_HANDLE;
  }
  vertexBufferSize_ = verts.size() * sizeof(ptdc::Vertex);
  if (vertexBufferSize_ == 0) return;

  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = vertexBufferSize_;
  bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  vkCheck(vkCreateBuffer(dev, &bci, nullptr, &vertexBuffer_), "vkCreateBuffer(vertex)");
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(dev, vertexBuffer_, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = ctx_.findMemoryType(
      req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  vkCheck(vkAllocateMemory(dev, &mai, nullptr, &vertexMemory_), "vkAllocateMemory(vertex)");
  vkCheck(vkBindBufferMemory(dev, vertexBuffer_, vertexMemory_, 0), "vkBindBufferMemory");
  void* mapped = nullptr;
  vkCheck(vkMapMemory(dev, vertexMemory_, 0, vertexBufferSize_, 0, &mapped), "vkMapMemory");
  std::memcpy(mapped, verts.data(), vertexBufferSize_);
  vkUnmapMemory(dev, vertexMemory_);
}

void Renderer::recordDraws(VkCommandBuffer cmd, const std::vector<ptdc::DrawCall>& draws,
                           const std::vector<VkBuffer>* checkpointBuffers) {
  VkDevice dev = ctx_.device();
  auto beginPass = [&](VkRenderPass rp) {
    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    bi.renderPass = rp;
    bi.framebuffer = framebuffer_;
    bi.renderArea = {{0, 0}, {width_, height_}};
    VkClearValue clear{};
    bi.clearValueCount = 1;
    bi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0.f, 1.f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, {width_, height_}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
  };
  auto snapshot = [&](VkBuffer dst) {
    VkImageMemoryBarrier toSrc{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toSrc.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toSrc.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.image = colorImage_;
    toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSrc);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width_, height_, 1};
    vkCmdCopyImageToBuffer(cmd, colorImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, 1, &region);
    VkImageMemoryBarrier toAtt{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toAtt.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toAtt.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toAtt.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toAtt.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toAtt.image = colorImage_;
    toAtt.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toAtt);
  };

  VkDeviceSize offset = 0;
  beginPass(renderPassClear_);
  for (size_t i = 0; i < draws.size(); ++i) {
    const auto& d = draws[i];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineFor(d.topology));
    if (vertexBuffer_)
      vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &offset);
    vkCmdDraw(cmd, d.vertexCount, 1, d.firstVertex, 0);
    if (checkpointBuffers) {
      vkCmdEndRenderPass(cmd);
      snapshot((*checkpointBuffers)[i]);
      if (i + 1 < draws.size()) beginPass(renderPassLoad_);
    }
  }
  if (!checkpointBuffers) {
    vkCmdEndRenderPass(cmd);
  }
  (void)dev;
}

Image Renderer::readBack() {
  VkDevice dev = ctx_.device();
  VkDeviceSize size = static_cast<VkDeviceSize>(width_) * height_ * 4;
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer staging = VK_NULL_HANDLE;
  vkCheck(vkCreateBuffer(dev, &bci, nullptr, &staging), "vkCreateBuffer(staging)");
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(dev, staging, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = ctx_.findMemoryType(
      req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkDeviceMemory mem = VK_NULL_HANDLE;
  vkCheck(vkAllocateMemory(dev, &mai, nullptr, &mem), "vkAllocateMemory(staging)");
  vkBindBufferMemory(dev, staging, mem, 0);

  ctx_.oneShot([&](VkCommandBuffer cmd) {
    VkImageMemoryBarrier toSrc{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toSrc.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toSrc.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.image = colorImage_;
    toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSrc);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width_, height_, 1};
    vkCmdCopyImageToBuffer(cmd, colorImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1,
                           &region);
  });

  Image img;
  img.width = width_;
  img.height = height_;
  img.rgba.resize(static_cast<size_t>(size));
  void* mapped = nullptr;
  vkCheck(vkMapMemory(dev, mem, 0, size, 0, &mapped), "vkMapMemory");
  std::memcpy(img.rgba.data(), mapped, static_cast<size_t>(size));
  vkUnmapMemory(dev, mem);
  vkDestroyBuffer(dev, staging, nullptr);
  vkFreeMemory(dev, mem, nullptr);
  return img;
}

Image Renderer::renderFrame(const std::vector<ptdc::DrawCall>& draws) {
  ctx_.oneShot([&](VkCommandBuffer cmd) { recordDraws(cmd, draws, nullptr); });
  return readBack();
}

std::vector<Image> Renderer::renderWithCheckpoints(const std::vector<ptdc::DrawCall>& draws) {
  VkDevice dev = ctx_.device();
  VkDeviceSize size = static_cast<VkDeviceSize>(width_) * height_ * 4;
  std::vector<VkBuffer> buffers(draws.size(), VK_NULL_HANDLE);
  std::vector<VkDeviceMemory> memories(draws.size(), VK_NULL_HANDLE);
  for (size_t i = 0; i < draws.size(); ++i) {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    vkCheck(vkCreateBuffer(dev, &bci, nullptr, &buffers[i]), "vkCreateBuffer(checkpoint)");
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, buffers[i], &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = ctx_.findMemoryType(
        req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkCheck(vkAllocateMemory(dev, &mai, nullptr, &memories[i]), "vkAllocateMemory(checkpoint)");
    vkBindBufferMemory(dev, buffers[i], memories[i], 0);
  }

  ctx_.oneShot([&](VkCommandBuffer cmd) { recordDraws(cmd, draws, &buffers); });

  std::vector<Image> out(draws.size());
  for (size_t i = 0; i < draws.size(); ++i) {
    out[i].width = width_;
    out[i].height = height_;
    out[i].rgba.resize(static_cast<size_t>(size));
    void* mapped = nullptr;
    vkCheck(vkMapMemory(dev, memories[i], 0, size, 0, &mapped), "vkMapMemory");
    std::memcpy(out[i].rgba.data(), mapped, static_cast<size_t>(size));
    vkUnmapMemory(dev, memories[i]);
    vkDestroyBuffer(dev, buffers[i], nullptr);
    vkFreeMemory(dev, memories[i], nullptr);
  }
  return out;
}
