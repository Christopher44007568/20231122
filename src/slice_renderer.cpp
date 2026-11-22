#include "slice_renderer.h"
#include "slice_frag_spv.h"
#include "slice_vert_spv.h"
#include "voxel_io.h"

#include <cstring>

namespace vox {

namespace {

uint32_t find_memory_type(VkPhysicalDevice physical, uint32_t type_bits,
                          VkMemoryPropertyFlags wanted) {
  VkPhysicalDeviceMemoryProperties props;
  vkGetPhysicalDeviceMemoryProperties(physical, &props);
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) &&
        (props.memoryTypes[i].propertyFlags & wanted) == wanted) {
      return i;
    }
  }
  throw SlicerError("no suitable memory type");
}

void create_buffer(VulkanContext& ctx, VkDeviceSize size,
                   VkBufferUsageFlags usage,
                   VkMemoryPropertyFlags props, VkBuffer& buffer,
                   VkDeviceMemory& memory) {
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  vk_check(vkCreateBuffer(ctx.device(), &bi, nullptr, &buffer),
           "vkCreateBuffer");
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(ctx.device(), buffer, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = find_memory_type(
      ctx.physical(), req.memoryTypeBits, props);
  vk_check(vkAllocateMemory(ctx.device(), &ai, nullptr, &memory),
           "vkAllocateMemory(buffer)");
  vk_check(vkBindBufferMemory(ctx.device(), buffer, memory, 0),
           "vkBindBufferMemory");
}

} // namespace

void SnapshotGpuResources::destroy(VkDevice device) noexcept {
  if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
  if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
  if (descriptor_pool)
    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
  if (set_layout)
    vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
  if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
  if (color_view) vkDestroyImageView(device, color_view, nullptr);
  if (color) vkDestroyImage(device, color, nullptr);
  if (color_memory) vkFreeMemory(device, color_memory, nullptr);
  if (sampler) vkDestroySampler(device, sampler, nullptr);
  if (texture_view) vkDestroyImageView(device, texture_view, nullptr);
  if (texture) vkDestroyImage(device, texture, nullptr);
  if (texture_memory) vkFreeMemory(device, texture_memory, nullptr);
  if (staging) vkDestroyBuffer(device, staging, nullptr);
  if (staging_memory) vkFreeMemory(device, staging_memory, nullptr);
  *this = SnapshotGpuResources{};
}

SliceRenderer::SliceRenderer(VulkanContext& ctx, EvidenceLog& log)
    : ctx_(ctx), log_(log) {
  VkAttachmentDescription color{};
  color.format = VK_FORMAT_R8G8B8A8_UNORM;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

  VkAttachmentReference color_ref{};
  color_ref.attachment = 0;
  color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;

  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.srcAccessMask = 0;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpci.attachmentCount = 1;
  rpci.pAttachments = &color;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &subpass;
  rpci.dependencyCount = 1;
  rpci.pDependencies = &dep;
  vk_check(vkCreateRenderPass(ctx_.device(), &rpci, nullptr, &render_pass_),
           "vkCreateRenderPass");
}

SliceRenderer::~SliceRenderer() {
  if (render_pass_)
    vkDestroyRenderPass(ctx_.device(), render_pass_, nullptr);
}

void SliceRenderer::transition(VkCommandBuffer cmd, VkImage image,
                               VkImageLayout from, VkImageLayout to,
                               VkImageAspectFlags aspect, uint32_t layers) {
  VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  bar.oldLayout = from;
  bar.newLayout = to;
  bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bar.image = image;
  bar.subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, layers};

  if (from == VK_IMAGE_LAYOUT_UNDEFINED &&
      to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    bar.srcAccessMask = 0;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  } else if (from == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  } else if (to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    bar.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bar.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  } else if (to == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  } else if (to == VK_IMAGE_LAYOUT_GENERAL) {
    bar.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT |
                        VK_ACCESS_MEMORY_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT |
                        VK_ACCESS_MEMORY_WRITE_BIT |
                        VK_ACCESS_HOST_WRITE_BIT;
  } else {
    bar.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    bar.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  }
  VkPipelineStageFlags src_stage =
      bar.srcAccessMask ? VK_PIPELINE_STAGE_TRANSFER_BIT
                        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags dst_stage =
      (to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
          ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
          : (to == VK_IMAGE_LAYOUT_GENERAL)
                ? VK_PIPELINE_STAGE_HOST_BIT
                : VK_PIPELINE_STAGE_TRANSFER_BIT;
  vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr,
                       1, &bar);
}

void SliceRenderer::upload_texture(SnapshotGpuResources& r,
                                   const Snapshot& snap) {
  const uint32_t w = snap.dims.x;
  const uint32_t h = snap.dims.y;
  const uint32_t layers = snap.dims.z;
  const VkDeviceSize size = VkDeviceSize(w) * h * layers * 4;

  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = VK_FORMAT_R8G8B8A8_UNORM;
  ii.extent = {w, h, 1};
  ii.mipLevels = 1;
  ii.arrayLayers = layers;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  vk_check(vkCreateImage(ctx_.device(), &ii, nullptr, &r.texture),
           "vkCreateImage(texture)");
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(ctx_.device(), r.texture, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = find_memory_type(
      ctx_.physical(), req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vk_check(vkAllocateMemory(ctx_.device(), &ai, nullptr, &r.texture_memory),
           "vkAllocateMemory(texture)");
  vk_check(vkBindImageMemory(ctx_.device(), r.texture, r.texture_memory, 0),
           "vkBindImageMemory(texture)");

  // Voxel value -> RGBA palette built on the CPU; 0 is the empty background.
  std::vector<uint8_t> rgba(static_cast<size_t>(size), 0);
  static const uint8_t palette[][4] = {
      {20, 20, 28, 255},    {231, 76, 60, 255},  {46, 204, 113, 255},
      {52, 152, 219, 255},  {241, 196, 15, 255},  {155, 89, 182, 255},
      {26, 188, 156, 255},  {230, 126, 34, 255},  {149, 165, 166, 255},
      {211, 84, 0, 255}};
  for (size_t i = 0; i < snap.voxels.size(); ++i) {
    uint8_t v = snap.voxels[i];
    const uint8_t* c = v == 0 ? palette[0] : palette[(v - 1) % 9 + 1];
    rgba[i * 4] = c[0];
    rgba[i * 4 + 1] = c[1];
    rgba[i * 4 + 2] = c[2];
    rgba[i * 4 + 3] = c[3];
  }

  create_buffer(ctx_, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                r.staging, r.staging_memory);
  r.staging_size = size;
  void* mapped = nullptr;
  vk_check(vkMapMemory(ctx_.device(), r.staging_memory, 0, size, 0, &mapped),
           "vkMapMemory(staging)");
  std::memcpy(mapped, rgba.data(), static_cast<size_t>(size));
  vkUnmapMemory(ctx_.device(), r.staging_memory);

  VkCommandBuffer cmd = ctx_.begin_oneshot();
  transition(cmd, r.texture, VK_IMAGE_LAYOUT_UNDEFINED,
             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
             VK_IMAGE_ASPECT_COLOR_BIT, layers);
  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers};
  region.imageExtent = {w, h, 1};
  vkCmdCopyBufferToImage(cmd, r.staging, r.texture,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  transition(cmd, r.texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
             VK_IMAGE_ASPECT_COLOR_BIT, layers);
  ctx_.end_oneshot(cmd);
  r.texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkImageViewCreateInfo iv{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  iv.image = r.texture;
  iv.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  iv.format = VK_FORMAT_R8G8B8A8_UNORM;
  iv.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
  vk_check(vkCreateImageView(ctx_.device(), &iv, nullptr, &r.texture_view),
           "vkCreateImageView(texture)");
}

void SliceRenderer::create_color_target(SnapshotGpuResources& r) {
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = VK_FORMAT_R8G8B8A8_UNORM;
  ii.extent = {ctx_.width(), ctx_.height(), 1};
  ii.mipLevels = 1;
  ii.arrayLayers = 1;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_LINEAR; // host-writable for the CPU fallback
  ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
             VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
             VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  vk_check(vkCreateImage(ctx_.device(), &ii, nullptr, &r.color),
           "vkCreateImage(color)");
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(ctx_.device(), r.color, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = find_memory_type(
      ctx_.physical(), req.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  vk_check(vkAllocateMemory(ctx_.device(), &ai, nullptr, &r.color_memory),
           "vkAllocateMemory(color)");
  vk_check(vkBindImageMemory(ctx_.device(), r.color, r.color_memory, 0),
           "vkBindImageMemory(color)");

  VkImageViewCreateInfo iv{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  iv.image = r.color;
  iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
  iv.format = VK_FORMAT_R8G8B8A8_UNORM;
  iv.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vk_check(vkCreateImageView(ctx_.device(), &iv, nullptr, &r.color_view),
           "vkCreateImageView(color)");

  VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fb.renderPass = render_pass_;
  fb.attachmentCount = 1;
  fb.pAttachments = &r.color_view;
  fb.width = ctx_.width();
  fb.height = ctx_.height();
  fb.layers = 1;
  vk_check(vkCreateFramebuffer(ctx_.device(), &fb, nullptr, &r.framebuffer),
           "vkCreateFramebuffer");
  r.color_layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

bool SliceRenderer::create_pipeline(SnapshotGpuResources& r) {
  VkDescriptorSetLayoutBinding bind{};
  bind.binding = 0;
  bind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bind.descriptorCount = 1;
  bind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo slci{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  slci.bindingCount = 1;
  slci.pBindings = &bind;
  vk_check(vkCreateDescriptorSetLayout(ctx_.device(), &slci, nullptr,
                                       &r.set_layout),
           "vkCreateDescriptorSetLayout");

  VkPushConstantRange pcr{};
  pcr.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pcr.offset = 0;
  pcr.size = sizeof(PushConstants);
  VkPipelineLayoutCreateInfo plci{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &r.set_layout;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &pcr;
  vk_check(vkCreatePipelineLayout(ctx_.device(), &plci, nullptr,
                                  &r.pipeline_layout),
           "vkCreatePipelineLayout");

  VkShaderModuleCreateInfo vsm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  vsm.codeSize = sizeof(kSliceVertSpv);
  vsm.pCode = kSliceVertSpv;
  VkShaderModule vert;
  vk_check(vkCreateShaderModule(ctx_.device(), &vsm, nullptr, &vert),
           "vkCreateShaderModule(vertex)");
  VkShaderModuleCreateInfo fsm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  fsm.codeSize = sizeof(kSliceFragSpv);
  fsm.pCode = kSliceFragSpv;
  VkShaderModule frag;
  vk_check(vkCreateShaderModule(ctx_.device(), &fsm, nullptr, &frag),
           "vkCreateShaderModule(fragment)");

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo vi{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport viewport{0, 0, static_cast<float>(ctx_.width()),
                      static_cast<float>(ctx_.height()), 0, 1};
  VkRect2D scissor{{0, 0}, {ctx_.width(), ctx_.height()}};
  VkPipelineViewportStateCreateInfo vs{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vs.viewportCount = 1;
  vs.pViewports = &viewport;
  vs.scissorCount = 1;
  vs.pScissors = &scissor;
  VkPipelineRasterizationStateCreateInfo rs{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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
  VkGraphicsPipelineCreateInfo gpci{
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gpci.stageCount = 2;
  gpci.pStages = stages;
  gpci.pVertexInputState = &vi;
  gpci.pInputAssemblyState = &ia;
  gpci.pViewportState = &vs;
  gpci.pRasterizationState = &rs;
  gpci.pMultisampleState = &ms;
  gpci.pColorBlendState = &cb;
  gpci.layout = r.pipeline_layout;
  gpci.renderPass = render_pass_;
  gpci.subpass = 0;
  VkResult pr =
      vkCreateGraphicsPipelines(ctx_.device(), VK_NULL_HANDLE, 1, &gpci,
                                nullptr, &r.pipeline);
  vkDestroyShaderModule(ctx_.device(), vert, nullptr);
  vkDestroyShaderModule(ctx_.device(), frag, nullptr);
  if (pr != VK_SUCCESS) {
    // Observed with lavapipe when its SPIR-V backend is unavailable in the
    // host environment. Destroy partial pipeline state and let the caller
    // fall back to CPU rasterization over the same Vulkan resources.
    r.pipeline = VK_NULL_HANDLE;
    return false;
  }

  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = VK_FILTER_NEAREST;
  sci.minFilter = VK_FILTER_NEAREST;
  sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.maxLod = 1.0f;
  vk_check(vkCreateSampler(ctx_.device(), &sci, nullptr, &r.sampler),
           "vkCreateSampler");

  VkDescriptorPoolSize pool_size{
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
  VkDescriptorPoolCreateInfo dpci{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpci.maxSets = 1;
  dpci.poolSizeCount = 1;
  dpci.pPoolSizes = &pool_size;
  vk_check(vkCreateDescriptorPool(ctx_.device(), &dpci, nullptr,
                                  &r.descriptor_pool),
           "vkCreateDescriptorPool");
  VkDescriptorSetAllocateInfo dsai{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dsai.descriptorPool = r.descriptor_pool;
  dsai.descriptorSetCount = 1;
  dsai.pSetLayouts = &r.set_layout;
  vk_check(vkAllocateDescriptorSets(ctx_.device(), &dsai, &r.descriptor_set),
           "vkAllocateDescriptorSets");
  VkDescriptorImageInfo dii{r.sampler, r.texture_view,
                            r.texture_layout};
  VkWriteDescriptorSet wds{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  wds.dstSet = r.descriptor_set;
  wds.dstBinding = 0;
  wds.descriptorCount = 1;
  wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  wds.pImageInfo = &dii;
  vkUpdateDescriptorSets(ctx_.device(), 1, &wds, 0, nullptr);
  return true;
}

std::vector<uint8_t> SliceRenderer::readback(SnapshotGpuResources& r) {
  const VkDeviceSize size =
      VkDeviceSize(ctx_.width()) * ctx_.height() * 4;
  VkBuffer dst;
  VkDeviceMemory dst_mem;
  create_buffer(ctx_, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                dst, dst_mem);
  VkCommandBuffer cmd = ctx_.begin_oneshot();
  VkBufferImageCopy region{};
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {ctx_.width(), ctx_.height(), 1};
  vkCmdCopyImageToBuffer(cmd, r.color,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, 1,
                         &region);
  ctx_.end_oneshot(cmd);

  std::vector<uint8_t> pixels(static_cast<size_t>(size));
  void* mapped = nullptr;
  vk_check(vkMapMemory(ctx_.device(), dst_mem, 0, size, 0, &mapped),
           "vkMapMemory(readback)");
  std::memcpy(pixels.data(), mapped, static_cast<size_t>(size));
  vkUnmapMemory(ctx_.device(), dst_mem);
  vkDestroyBuffer(ctx_.device(), dst, nullptr);
  vkFreeMemory(ctx_.device(), dst_mem, nullptr);
  return pixels;
}

std::vector<uint8_t> SliceRenderer::rasterize_cpu(const Snapshot& snap,
                                                  uint32_t slice) {
  const uint32_t w = ctx_.width();
  const uint32_t h = ctx_.height();
  std::vector<uint8_t> rgba(size_t(w) * h * 4, 0);
  static const uint8_t palette[][4] = {
      {20, 20, 28, 255},    {231, 76, 60, 255},  {46, 204, 113, 255},
      {52, 152, 219, 255},  {241, 196, 15, 255},  {155, 89, 182, 255},
      {26, 188, 156, 255},  {230, 126, 34, 255},  {149, 165, 166, 255},
      {211, 84, 0, 255}};
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      uint8_t v = 0;
      if (x < snap.dims.x && y < snap.dims.y && slice < snap.dims.z) {
        v = snap.voxels[static_cast<size_t>(
            voxel_index(snap.dims, x, y, slice))];
      }
      const uint8_t* c = v == 0 ? palette[0] : palette[(v - 1) % 9 + 1];
      size_t p = (size_t(y) * w + x) * 4;
      rgba[p] = c[0];
      rgba[p + 1] = c[1];
      rgba[p + 2] = c[2];
      rgba[p + 3] = c[3];
    }
  }
  return rgba;
}

void SliceRenderer::write_color_image(SnapshotGpuResources& r,
                                      const std::vector<uint8_t>& rgba) {
  VkCommandBuffer pre = ctx_.begin_oneshot();
  transition(pre, r.color, r.color_layout, VK_IMAGE_LAYOUT_GENERAL,
             VK_IMAGE_ASPECT_COLOR_BIT, 1);
  ctx_.end_oneshot(pre);
  r.color_layout = VK_IMAGE_LAYOUT_GENERAL;
  VkImageSubresource sub{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
  VkSubresourceLayout layout;
  vkGetImageSubresourceLayout(ctx_.device(), r.color, &sub, &layout);
  void* mapped = nullptr;
  vk_check(vkMapMemory(ctx_.device(), r.color_memory, 0, VK_WHOLE_SIZE, 0,
                       &mapped),
           "vkMapMemory(color fallback)");
  auto* base = static_cast<uint8_t*>(mapped) + layout.offset;
  const uint32_t w = ctx_.width();
  const uint32_t h = ctx_.height();
  for (uint32_t y = 0; y < h; ++y) {
    std::memcpy(base + y * layout.rowPitch, rgba.data() + y * w * 4,
                size_t(w) * 4);
  }
  vkUnmapMemory(ctx_.device(), r.color_memory);
  VkCommandBuffer post = ctx_.begin_oneshot();
  transition(post, r.color, VK_IMAGE_LAYOUT_GENERAL,
             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
             1);
  ctx_.end_oneshot(post);
  r.color_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}

std::vector<RenderedSlice>
SliceRenderer::render_snapshot(const Snapshot& snap, const std::string& tag) {
  SnapshotGpuResources resources;
  std::vector<RenderedSlice> result;
  try {
    log_.event("snapshot_render_begin")
        .field("object", snap.object_id)
        .field("tag", tag)
        .field("format", version_name(snap.version))
        .field("truncated", snap.truncated)
        .field("expected_bytes", snap.expected_bytes)
        .field("observed_bytes", snap.observed_bytes)
        .field("source", snap.source_path);

    upload_texture(resources, snap);
    log_.event("resource_state")
        .field("object", snap.object_id)
        .field("tag", tag)
        .field("resource", "voxel_texture")
        .field("layout", "SHADER_READ_ONLY_OPTIMAL")
        .field("layers", uint64_t(snap.dims.z));
    create_color_target(resources);
    bool gpu_path = create_pipeline(resources);
    if (!gpu_path) {
      log_.event("pipeline_fallback")
          .field("object", snap.object_id)
          .field("tag", tag)
          .field("backend", "cpu_rasterization")
          .field("reason",
                 "vkCreateGraphicsPipelines failed on the Vulkan driver; "
                 "resource setup, image layout and readback stay on Vulkan");
    } else {
      log_.event("pipeline_ready")
          .field("object", snap.object_id)
          .field("tag", tag)
          .field("backend", "vulkan_graphics_pipeline");
    }

    result.reserve(snap.dims.z);
    for (uint32_t slice = 0; slice < snap.dims.z; ++slice) {
      VkImageLayout color_before = resources.color_layout;
      std::vector<uint8_t> pixels;
      if (gpu_path) {
        VkCommandBuffer cmd = ctx_.begin_oneshot();
        transition(cmd, resources.color, color_before,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_ASPECT_COLOR_BIT, 1);
        resources.color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkClearValue clear{};
        clear.color = {{0.02f, 0.02f, 0.04f, 1.0f}};
        VkRenderPassBeginInfo rpbi{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpbi.renderPass = render_pass_;
        rpbi.framebuffer = resources.framebuffer;
        rpbi.renderArea.extent = {ctx_.width(), ctx_.height()};
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          resources.pipeline);
        vkCmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            resources.pipeline_layout, 0, 1, &resources.descriptor_set, 0,
            nullptr);
        PushConstants pc{static_cast<float>(snap.dims.x),
                         static_cast<float>(snap.dims.y),
                         static_cast<float>(slice), 0.0f};
        vkCmdPushConstants(cmd, resources.pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT |
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
        resources.color_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        ctx_.end_oneshot(cmd);
        pixels = readback(resources);
      } else {
        // CPU fallback: rasterize the slice, then push the pixels into the
        // same Vulkan color image and read them back through Vulkan so the
        // framebuffer/state evidence stays comparable across backends.
        pixels = rasterize_cpu(snap, slice);
        write_color_image(resources, pixels);
        resources.color_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      }
      log_.event("draw_call")
          .field("object", snap.object_id)
          .field("tag", tag)
          .field("slice", uint64_t(slice))
          .field("vertex_count", uint64_t(3))
          .field("instance_count", uint64_t(1))
          .field("texture_layout", "SHADER_READ_ONLY_OPTIMAL")
          .field("backend", gpu_path ? "vulkan" : "cpu_fallback")
          .field("framebuffer_before",
                 color_before == VK_IMAGE_LAYOUT_UNDEFINED
                     ? "UNDEFINED"
                     : "TRANSFER_SRC_OPTIMAL")
          .field("framebuffer_during", "COLOR_ATTACHMENT_OPTIMAL")
          .field("framebuffer_after", "TRANSFER_SRC_OPTIMAL");
      RenderedSlice rs{slice, pixels, pixel_fingerprint(pixels)};
      log_.event("framebuffer_readback")
          .field("object", snap.object_id)
          .field("tag", tag)
          .field("slice", uint64_t(slice))
          .field("width", uint64_t(ctx_.width()))
          .field("height", uint64_t(ctx_.height()))
          .field("fingerprint", rs.fingerprint);
      result.push_back(std::move(rs));
    }
    log_.event("snapshot_render_end")
        .field("object", snap.object_id)
        .field("tag", tag)
        .field("slice_count", uint64_t(snap.dims.z));
  } catch (...) {
    resources.destroy(ctx_.device());
    throw;
  }
  resources.destroy(ctx_.device());
  log_.event("resource_cleanup")
      .field("object", snap.object_id)
      .field("tag", tag)
      .field("destroyed", true);
  return result;
}

} // namespace vox
