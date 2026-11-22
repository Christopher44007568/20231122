#include "renderer.h"

#include <algorithm>
#include <cstring>

#include "util.h"

namespace tar {
namespace {

constexpr int kPageSize = 128;  // texels per array layer edge

// Deterministic page content: color-coded by page id with a diagonal
// stripe. Frame-independent and identical across runs, so any
// framebuffer difference is attributable to paging behavior alone.
std::vector<uint8_t> GeneratePage(int page) {
  std::vector<uint8_t> px(kPageSize * kPageSize * 4);
  const uint8_t r = static_cast<uint8_t>((page * 47 + 31) % 256);
  const uint8_t g = static_cast<uint8_t>((page * 89 + 77) % 256);
  const uint8_t b = static_cast<uint8_t>((page * 131 + 113) % 256);
  for (int y = 0; y < kPageSize; ++y) {
    for (int x = 0; x < kPageSize; ++x) {
      size_t i = (static_cast<size_t>(y) * kPageSize + x) * 4;
      bool stripe = ((x + y) / 16) % 2 == 0;
      bool border = x < 4 || y < 4 || x >= kPageSize - 4 || y >= kPageSize - 4;
      px[i + 0] = border ? 255 : (stripe ? r : static_cast<uint8_t>(r / 3));
      px[i + 1] = border ? 255 : (stripe ? g : static_cast<uint8_t>(g / 3));
      px[i + 2] = border ? 255 : (stripe ? b : static_cast<uint8_t>(b / 3));
      px[i + 3] = 255;
    }
  }
  return px;
}

}  // namespace

Renderer::Renderer(VkContext& ctx, const RenderConfig& cfg, Log& log)
    : ctx_(ctx), cfg_(cfg), log_(log) {
  CreateRenderTarget();
  CreateTextureArray();
  CreatePipeline();
}

Renderer::~Renderer() {
  VkDevice dev = ctx_.Device();
  if (pipeline_) vkDestroyPipeline(dev, pipeline_, nullptr);
  if (pipe_layout_) vkDestroyPipelineLayout(dev, pipe_layout_, nullptr);
  if (desc_pool_) vkDestroyDescriptorPool(dev, desc_pool_, nullptr);
  if (desc_layout_) vkDestroyDescriptorSetLayout(dev, desc_layout_, nullptr);
  if (sampler_) vkDestroySampler(dev, sampler_, nullptr);
  if (array_view_) vkDestroyImageView(dev, array_view_, nullptr);
  if (array_) vkDestroyImage(dev, array_, nullptr);
  if (array_mem_) vkFreeMemory(dev, array_mem_, nullptr);
  if (framebuffer_) vkDestroyFramebuffer(dev, framebuffer_, nullptr);
  if (render_pass_clear_) vkDestroyRenderPass(dev, render_pass_clear_, nullptr);
  if (render_pass_load_) vkDestroyRenderPass(dev, render_pass_load_, nullptr);
  if (color_view_) vkDestroyImageView(dev, color_view_, nullptr);
  if (color_) vkDestroyImage(dev, color_, nullptr);
  if (color_mem_) vkFreeMemory(dev, color_mem_, nullptr);
}

void Renderer::CreateRenderTarget() {
  VkDevice dev = ctx_.Device();
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {static_cast<uint32_t>(cfg_.width),
                static_cast<uint32_t>(cfg_.height), 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VK_CHECK(vkCreateImage(dev, &ici, nullptr, &color_));
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(dev, color_, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = ctx_.FindMemoryType(req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VK_CHECK(vkAllocateMemory(dev, &ai, nullptr, &color_mem_));
  VK_CHECK(vkBindImageMemory(dev, color_, color_mem_, 0));

  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = color_;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = ici.format;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VK_CHECK(vkCreateImageView(dev, &vci, nullptr, &color_view_));

  // Two compatible render passes: the first object of a frame clears,
  // subsequent objects load the existing contents.
  VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &ref;
  for (int variant = 0; variant < 2; ++variant) {
    VkAttachmentDescription att{};
    att.format = ici.format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = variant == 0 ? VK_ATTACHMENT_LOAD_OP_CLEAR
                              : VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = variant == 0
                            ? VK_IMAGE_LAYOUT_UNDEFINED
                            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    VK_CHECK(vkCreateRenderPass(dev, &rpci, nullptr,
                                variant == 0 ? &render_pass_clear_
                                             : &render_pass_load_));
  }

  VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fci.renderPass = render_pass_clear_;
  fci.attachmentCount = 1;
  fci.pAttachments = &color_view_;
  fci.width = ici.extent.width;
  fci.height = ici.extent.height;
  fci.layers = 1;
  VK_CHECK(vkCreateFramebuffer(dev, &fci, nullptr, &framebuffer_));
}

void Renderer::CreateTextureArray() {
  VkDevice dev = ctx_.Device();
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {kPageSize, kPageSize, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = static_cast<uint32_t>(cfg_.paging.num_layers);
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VK_CHECK(vkCreateImage(dev, &ici, nullptr, &array_));
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(dev, array_, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = ctx_.FindMemoryType(req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VK_CHECK(vkAllocateMemory(dev, &ai, nullptr, &array_mem_));
  VK_CHECK(vkBindImageMemory(dev, array_, array_mem_, 0));

  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = array_;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  vci.format = ici.format;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                          static_cast<uint32_t>(cfg_.paging.num_layers)};
  VK_CHECK(vkCreateImageView(dev, &vci, nullptr, &array_view_));

  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = VK_FILTER_NEAREST;
  sci.minFilter = VK_FILTER_NEAREST;
  sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  VK_CHECK(vkCreateSampler(dev, &sci, nullptr, &sampler_));

  // Transition all layers to SHADER_READ_ONLY once; uploads temporarily
  // switch individual layers to TRANSFER_DST and back.
  VkCommandBuffer cmd = ctx_.BeginCommands();
  VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bar.image = array_;
  bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                          static_cast<uint32_t>(cfg_.paging.num_layers)};
  bar.srcAccessMask = 0;
  bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &bar);
  ctx_.EndCommands(cmd);

  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo lci{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  lci.bindingCount = 1;
  lci.pBindings = &binding;
  VK_CHECK(vkCreateDescriptorSetLayout(dev, &lci, nullptr, &desc_layout_));

  VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
  VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pci.maxSets = 1;
  pci.poolSizeCount = 1;
  pci.pPoolSizes = &pool_size;
  VK_CHECK(vkCreateDescriptorPool(dev, &pci, nullptr, &desc_pool_));

  VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dai.descriptorPool = desc_pool_;
  dai.descriptorSetCount = 1;
  dai.pSetLayouts = &desc_layout_;
  VK_CHECK(vkAllocateDescriptorSets(dev, &dai, &desc_set_));

  VkDescriptorImageInfo dii{};
  dii.sampler = sampler_;
  dii.imageView = array_view_;
  dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = desc_set_;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &dii;
  vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
}

void Renderer::CreatePipeline() {
  VkDevice dev = ctx_.Device();
  auto load_shader = [&](const char* name) {
    std::vector<uint8_t> spv =
        ReadFile(JoinPath(cfg_.shader_dir, std::string(name) + ".spv"));
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = spv.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(spv.data());
    VkShaderModule mod;
    VK_CHECK(vkCreateShaderModule(dev, &ci, nullptr, &mod));
    return mod;
  };
  VkShaderModule vs = load_shader("quad.vert");
  VkShaderModule fs = load_shader("quad.frag");

  VkPushConstantRange pc{VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT,
                         0, 32};
  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &desc_layout_;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &pc;
  VK_CHECK(vkCreatePipelineLayout(dev, &plci, nullptr, &pipe_layout_));

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs;
  stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo vi{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport viewport{0, 0, static_cast<float>(cfg_.width),
                      static_cast<float>(cfg_.height), 0.0f, 1.0f};
  VkRect2D scissor{{0, 0},
                   {static_cast<uint32_t>(cfg_.width),
                    static_cast<uint32_t>(cfg_.height)}};
  VkPipelineViewportStateCreateInfo vp{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.pViewports = &viewport;
  vp.scissorCount = 1;
  vp.pScissors = &scissor;
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

  VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gpci.stageCount = 2;
  gpci.pStages = stages;
  gpci.pVertexInputState = &vi;
  gpci.pInputAssemblyState = &ia;
  gpci.pViewportState = &vp;
  gpci.pRasterizationState = &rs;
  gpci.pMultisampleState = &ms;
  gpci.pColorBlendState = &cb;
  gpci.layout = pipe_layout_;
  gpci.renderPass = render_pass_load_;  // compatible with the clear pass
  VK_CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, nullptr,
                                     &pipeline_));
  vkDestroyShaderModule(dev, vs, nullptr);
  vkDestroyShaderModule(dev, fs, nullptr);
}

// Records a page upload into the frame's command buffer. The staging
// buffer is kept alive in `staging_` until the frame is submitted, so
// uploads and draws execute in the exact order they were recorded.
void Renderer::RecordPageUpload(VkCommandBuffer cmd, int layer, int page) {
  VkDevice dev = ctx_.Device();
  std::vector<uint8_t> px = GeneratePage(page);
  const VkDeviceSize size = px.size();

  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  VkBuffer staging;
  VK_CHECK(vkCreateBuffer(dev, &bci, nullptr, &staging));
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(dev, staging, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = ctx_.FindMemoryType(
      req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkDeviceMemory mem;
  VK_CHECK(vkAllocateMemory(dev, &ai, nullptr, &mem));
  VK_CHECK(vkBindBufferMemory(dev, staging, mem, 0));
  void* mapped = nullptr;
  VK_CHECK(vkMapMemory(dev, mem, 0, size, 0, &mapped));
  std::memcpy(mapped, px.data(), size);
  vkUnmapMemory(dev, mem);
  staging_.push_back({staging, mem});

  VkImageMemoryBarrier to_dst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  to_dst.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_dst.image = array_;
  to_dst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                             static_cast<uint32_t>(layer), 1};
  to_dst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                       1, &to_dst);
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0,
                           static_cast<uint32_t>(layer), 1};
  copy.imageExtent = {kPageSize, kPageSize, 1};
  vkCmdCopyBufferToImage(cmd, staging, array_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1, &copy);
  VkImageMemoryBarrier to_read = to_dst;
  to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &to_read);
}

void Renderer::ReleaseStaging() {
  VkDevice dev = ctx_.Device();
  for (auto& [buf, mem] : staging_) {
    vkDestroyBuffer(dev, buf, nullptr);
    vkFreeMemory(dev, mem, nullptr);
  }
  staging_.clear();
}

Image Renderer::Readback() {
  VkDevice dev = ctx_.Device();
  Image img;
  img.width = static_cast<uint32_t>(cfg_.width);
  img.height = static_cast<uint32_t>(cfg_.height);
  const VkDeviceSize size =
      static_cast<VkDeviceSize>(cfg_.width) * cfg_.height * 4;

  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer buf;
  VK_CHECK(vkCreateBuffer(dev, &bci, nullptr, &buf));
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(dev, buf, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = ctx_.FindMemoryType(
      req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkDeviceMemory mem;
  VK_CHECK(vkAllocateMemory(dev, &ai, nullptr, &mem));
  VK_CHECK(vkBindBufferMemory(dev, buf, mem, 0));

  VkCommandBuffer cmd = ctx_.BeginCommands();
  VkImageMemoryBarrier to_src{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  to_src.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_src.image = color_;
  to_src.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  to_src.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                       1, &to_src);
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {static_cast<uint32_t>(cfg_.width),
                      static_cast<uint32_t>(cfg_.height), 1};
  vkCmdCopyImageToBuffer(cmd, color_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf,
                         1, &copy);
  ctx_.EndCommands(cmd);

  void* mapped = nullptr;
  VK_CHECK(vkMapMemory(dev, mem, 0, size, 0, &mapped));
  img.rgba.resize(size);
  std::memcpy(img.rgba.data(), mapped, size);
  vkUnmapMemory(dev, mem);
  vkDestroyBuffer(dev, buf, nullptr);
  vkFreeMemory(dev, mem, nullptr);
  return img;
}

Snapshot Renderer::Run() {
  Snapshot snap;
  snap.label = cfg_.label;
  snap.frames = cfg_.frames;
  snap.width = cfg_.width;
  snap.height = cfg_.height;
  snap.objects = cfg_.grid * cfg_.grid;
  if (cfg_.paging.extension_enabled) {
    snap.extension = kEmulatedExtension;
    snap.extension_enabled = 1;
  }
  PageTable table(cfg_.paging);
  const int cell_w = cfg_.width / cfg_.grid;
  const int cell_h = cfg_.height / cfg_.grid;
  const int page_bytes = kPageSize * kPageSize * 4;

  for (int frame = 0; frame < cfg_.frames; ++frame) {
    std::vector<int> evicted_this_frame;
    VkCommandBuffer cmd = ctx_.BeginCommands();
    FramePromotions promo = table.BeginFrame();
    for (const auto& [page, layer] : promo.promoted) {
      RecordPageUpload(cmd, layer, page);  // v2: promoted content arrives
    }
    evicted_this_frame = promo.evicted;

    for (int obj = 0; obj < snap.objects; ++obj) {
      const int ox = (obj % cfg_.grid) * cell_w;
      const int oy = (obj / cfg_.grid) * cell_h;
      const int page = (obj + frame) % cfg_.paging.num_pages;
      // Error isolation: a failing object is recorded and skipped, the
      // remaining objects of the frame are still processed.
      try {
        if (std::find(cfg_.inject_fault_objs.begin(), cfg_.inject_fault_objs.end(),
                      obj) != cfg_.inject_fault_objs.end()) {
          throw PipelineError("injected upload failure for obj " +
                              std::to_string(obj));
        }
        PagingResult pr = table.Acquire(page);
        if (pr.evicted_page >= 0) evicted_this_frame.push_back(pr.evicted_page);
        if (pr.faulted) {
          PageEvent ev;
          ev.frame = frame;
          ev.kind = "pagefault";
          ev.page = page;
          ev.obj = obj;
          snap.events.push_back(ev);
        }
        if (pr.faulted && !pr.placeholder) {
          RecordPageUpload(cmd, pr.layer, page);  // baseline: sync page-in
        }
        // One render pass instance per object so that page uploads and
        // draws interleave in the correct order within this frame.
        VkClearValue clear{};
        clear.color = {{0.02f, 0.02f, 0.03f, 1.0f}};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = obj == 0 ? render_pass_clear_ : render_pass_load_;
        rp.framebuffer = framebuffer_;
        rp.renderArea = {{0, 0},
                         {static_cast<uint32_t>(cfg_.width),
                          static_cast<uint32_t>(cfg_.height)}};
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipe_layout_, 0, 1, &desc_set_, 0, nullptr);
        float pc[8] = {static_cast<float>(ox),
                       static_cast<float>(oy),
                       static_cast<float>(cell_w),
                       static_cast<float>(cell_h),
                       static_cast<float>(pr.layer),
                       pr.placeholder ? 1.0f : 0.0f,
                       0.0f,
                       0.0f};
        vkCmdPushConstants(cmd, pipe_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT |
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), pc);
        vkCmdDraw(cmd, 6, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        DrawCall d;
        d.frame = frame;
        d.obj = obj;
        d.layer = pr.layer;
        d.page = page;
        d.placeholder = pr.placeholder ? 1 : 0;
        d.x = ox;
        d.y = oy;
        d.w = cell_w;
        d.h = cell_h;
        snap.draws.push_back(d);
      } catch (const std::exception& e) {
        PageEvent ev;
        ev.frame = frame;
        ev.kind = "error";
        ev.page = page;
        ev.obj = obj;
        ev.message = e.what();
        snap.events.push_back(ev);
        log_.Warn(std::string("frame ") + std::to_string(frame) + " obj " +
                  std::to_string(obj) + " failed: " + e.what() +
                  " (continuing)");
      }
    }

    ctx_.EndCommands(cmd);
    ReleaseStaging();
    ctx_.PollEvents();

    ResourceState rs;
    rs.frame = frame;
    rs.resident = table.ResidentPages();
    rs.evicted = evicted_this_frame;
    rs.membytes = table.ResidentBytes(page_bytes);
    snap.resources.push_back(rs);

    Image img = Readback();
    FramebufferRef fb;
    fb.frame = frame;
    char name[64];
    std::snprintf(name, sizeof(name), "frame_%s_%03d.png", cfg_.label.c_str(),
                  frame);
    fb.file = name;
    fb.crc = Crc32(img.rgba.data(), img.rgba.size());
    WritePng(JoinPath(cfg_.evidence_dir, name), img);
    snap.framebuffers.push_back(fb);
  }
  return snap;
}

}  // namespace tar
