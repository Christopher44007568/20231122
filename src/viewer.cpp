// Interactive GLFW viewer: shows the selected shadow-cache depth map in a
// window and lets the user refresh individual cache levels while watching
// invalidation reasons and before/after depth comparison results.
#include "analyzer.h"
#include "spirv_gen.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Viewer {
  Analyzer analyzer;
  GLFWwindow* window = nullptr;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkFormat swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
  VkExtent2D extent{900, 900};
  std::vector<VkImage> images;
  std::vector<VkImageView> views;
  std::vector<VkFramebuffer> framebuffers;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
  VkDescriptorPool descPool = VK_NULL_HANDLE;
  VkDescriptorSet descSet = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  VkSemaphore semAcquire = VK_NULL_HANDLE;
  VkSemaphore semDone = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;

  Scenario scenario = Scenario::Static;
  bool animate = false;
  uint32_t frame = 0;
  size_t selLight = 0, selCascade = 0;
  double lastUnchanged = -1.0;
  std::string status = "select a cache level";
};

Viewer g;

VkShaderModule makeModule(VkDevice dev, const std::vector<uint32_t>& spv) {
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = spv.size() * 4;
  ci.pCode = spv.data();
  VkShaderModule m = VK_NULL_HANDLE;
  vkCreateShaderModule(dev, &ci, nullptr, &m);
  return m;
}

bool createSwapchain() {
  VkContext& ctx = g.analyzer.context();
  VkDevice dev = ctx.device();
  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice(), g.surface,
                                            &caps);
  uint32_t fmtCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice(), g.surface,
                                       &fmtCount, nullptr);
  std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice(), g.surface,
                                       &fmtCount, fmts.data());
  if (fmtCount == 0) return false;
  g.swapFormat = fmts[0].format;
  for (auto& f : fmts)
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM) g.swapFormat = f.format;
  g.extent = caps.currentExtent.width != UINT32_MAX
                 ? caps.currentExtent
                 : VkExtent2D{900, 900};

  VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  sci.surface = g.surface;
  sci.minImageCount = caps.minImageCount < 2 ? 2 : caps.minImageCount;
  sci.imageFormat = g.swapFormat;
  sci.imageColorSpace = fmts[0].colorSpace;
  sci.imageExtent = g.extent;
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  sci.clipped = VK_TRUE;
  if (vkCreateSwapchainKHR(dev, &sci, nullptr, &g.swapchain) != VK_SUCCESS)
    return false;

  uint32_t count = 0;
  vkGetSwapchainImagesKHR(dev, g.swapchain, &count, nullptr);
  g.images.resize(count);
  vkGetSwapchainImagesKHR(dev, g.swapchain, &count, g.images.data());
  g.views.resize(count);
  for (uint32_t i = 0; i < count; ++i) {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = g.images[i];
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = g.swapFormat;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(dev, &vi, nullptr, &g.views[i]) != VK_SUCCESS)
      return false;
  }
  return true;
}

bool createBlitPipeline() {
  VkContext& ctx = g.analyzer.context();
  VkDevice dev = ctx.device();

  VkAttachmentDescription att{};
  att.format = g.swapFormat;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &ref;
  VkRenderPassCreateInfo rci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rci.attachmentCount = 1;
  rci.pAttachments = &att;
  rci.subpassCount = 1;
  rci.pSubpasses = &sub;
  if (vkCreateRenderPass(dev, &rci, nullptr, &g.renderPass) != VK_SUCCESS)
    return false;

  g.framebuffers.resize(g.views.size());
  for (size_t i = 0; i < g.views.size(); ++i) {
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = g.renderPass;
    fci.attachmentCount = 1;
    fci.pAttachments = &g.views[i];
    fci.width = g.extent.width;
    fci.height = g.extent.height;
    fci.layers = 1;
    if (vkCreateFramebuffer(dev, &fci, nullptr, &g.framebuffers[i]) !=
        VK_SUCCESS)
      return false;
  }

  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo dli{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dli.bindingCount = 1;
  dli.pBindings = &binding;
  if (vkCreateDescriptorSetLayout(dev, &dli, nullptr, &g.setLayout) !=
      VK_SUCCESS)
    return false;

  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &g.setLayout;
  if (vkCreatePipelineLayout(dev, &plci, nullptr, &g.pipeLayout) != VK_SUCCESS)
    return false;

  VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
  VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpi.maxSets = 1;
  dpi.poolSizeCount = 1;
  dpi.pPoolSizes = &poolSize;
  if (vkCreateDescriptorPool(dev, &dpi, nullptr, &g.descPool) != VK_SUCCESS)
    return false;
  VkDescriptorSetAllocateInfo dai{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dai.descriptorPool = g.descPool;
  dai.descriptorSetCount = 1;
  dai.pSetLayouts = &g.setLayout;
  if (vkAllocateDescriptorSets(dev, &dai, &g.descSet) != VK_SUCCESS)
    return false;

  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = VK_FILTER_NEAREST;
  sci.minFilter = VK_FILTER_NEAREST;
  sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (vkCreateSampler(dev, &sci, nullptr, &g.sampler) != VK_SUCCESS)
    return false;

  VkShaderModule vert = makeModule(dev, genBlitVert());
  VkShaderModule frag = makeModule(dev, genBlitFrag());
  if (!vert || !frag) return false;
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
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
  VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT,
                                 VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dynStates;
  VkGraphicsPipelineCreateInfo pci{
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pci.stageCount = 2;
  pci.pStages = stages;
  pci.pVertexInputState = &vin;
  pci.pInputAssemblyState = &ia;
  pci.pViewportState = &vp;
  pci.pRasterizationState = &rs;
  pci.pMultisampleState = &ms;
  pci.pColorBlendState = &cb;
  pci.pDynamicState = &dyn;
  pci.layout = g.pipeLayout;
  pci.renderPass = g.renderPass;
  bool ok = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr,
                                      &g.pipeline) == VK_SUCCESS;
  vkDestroyShaderModule(dev, vert, nullptr);
  vkDestroyShaderModule(dev, frag, nullptr);
  return ok;
}

void clampSelection() {
  ShadowCacheSystem& cache = g.analyzer.cache();
  if (g.selLight >= cache.lightCount()) g.selLight = 0;
  if (g.selCascade >= cache.cascadeCount(g.selLight)) g.selCascade = 0;
}

void onKey(GLFWwindow*, int key, int, int action, int) {
  if (action != GLFW_PRESS) return;
  ShadowCacheSystem& cache = g.analyzer.cache();
  switch (key) {
  case GLFW_KEY_ESCAPE:
    glfwSetWindowShouldClose(g.window, GLFW_TRUE);
    break;
  case GLFW_KEY_TAB:
    g.selLight = (g.selLight + 1) % cache.lightCount();
    g.selCascade = 0;
    break;
  case GLFW_KEY_C:
    g.selCascade = (g.selCascade + 1) % cache.cascadeCount(g.selLight);
    break;
  case GLFW_KEY_R: {
    CompareResult cmp = g.analyzer.refreshAndCompare(g.selLight, g.selCascade,
                                                     ReasonManual);
    g.lastUnchanged = cmp.unchangedRatio;
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "manual refresh: unchanged %.2f%% (%zu/%zu texels)",
                  cmp.unchangedRatio * 100.0, cmp.unchangedTexels, cmp.texels);
    g.status = buf;
    std::printf("[viewer] %s\n", buf);
    break;
  }
  case GLFW_KEY_SPACE:
    g.animate = !g.animate;
    break;
  case GLFW_KEY_1:
  case GLFW_KEY_2:
  case GLFW_KEY_3:
    g.scenario = key == GLFW_KEY_1   ? Scenario::Static
                 : key == GLFW_KEY_2 ? Scenario::MovingLight
                                     : Scenario::MaterialChange;
    g.analyzer.resetScene();
    g.frame = 0;
    clampSelection();
    break;
  default:
    break;
  }
}

void updateTitle() {
  ShadowCacheSystem& cache = g.analyzer.cache();
  const CacheEntry& e = cache.entry(g.selLight, g.selCascade);
  ReasonDetail det =
      cache.computeReasons(g.analyzer.scene(), g.selLight, g.selCascade);
  std::string reasons;
  for (uint32_t bit : kAllReasonBits)
    if (det.bits & bit) {
      if (!reasons.empty()) reasons += ",";
      reasons += reasonName(bit);
    }
  if (reasons.empty()) reasons = "none";
  char title[256];
  std::snprintf(
      title, sizeof(title),
      "shadow-cache | %s/c%zu | scenario=%s %s | refreshes=%llu | "
      "pending: %s | unchanged=%.1f%%",
      g.analyzer.scene().lights[g.selLight].name.c_str(), g.selCascade,
      scenarioName(g.scenario), g.animate ? "(anim)" : "(paused)",
      (unsigned long long)e.refreshCount, reasons.c_str(),
      g.lastUnchanged * 100.0);
  glfwSetWindowTitle(g.window, title);
}

void drawFrame() {
  VkContext& ctx = g.analyzer.context();
  VkDevice dev = ctx.device();
  ShadowCacheSystem& cache = g.analyzer.cache();
  CacheEntry& e = cache.entry(g.selLight, g.selCascade);

  // Make the selected depth map sampleable.
  if (e.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && e.rendered) {
    VkCommandBuffer cmd = ctx.beginCommands();
    transitionImage(cmd, e.depth.image, e.layout,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
    ctx.endCommands(cmd);
    e.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }

  VkDescriptorImageInfo imgInfo{};
  imgInfo.sampler = g.sampler;
  imgInfo.imageView = e.depth.view;
  imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = g.descSet;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &imgInfo;
  vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

  vkWaitForFences(dev, 1, &g.fence, VK_TRUE, UINT64_MAX);
  vkResetFences(dev, 1, &g.fence);

  uint32_t imageIndex = 0;
  vkAcquireNextImageKHR(dev, g.swapchain, UINT64_MAX, g.semAcquire,
                        VK_NULL_HANDLE, &imageIndex);

  VkCommandBuffer cmd = ctx.beginCommands();
  VkClearValue clear{};
  clear.color = {{0.05f, 0.05f, 0.08f, 1.0f}};
  VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rbi.renderPass = g.renderPass;
  rbi.framebuffer = g.framebuffers[imageIndex];
  rbi.renderArea = {{0, 0}, g.extent};
  rbi.clearValueCount = 1;
  rbi.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
  VkViewport vp{0, 0, (float)g.extent.width, (float)g.extent.height, 0, 1};
  VkRect2D sc{{0, 0}, g.extent};
  vkCmdSetViewport(cmd, 0, 1, &vp);
  vkCmdSetScissor(cmd, 0, 1, &sc);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeLayout,
                          0, 1, &g.descSet, 0, nullptr);
  vkCmdDraw(cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);

  VkPipelineStageFlags waitStage =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &g.semAcquire;
  si.pWaitDstStageMask = &waitStage;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &g.semDone;
  vkQueueSubmit(ctx.queue(), 1, &si, g.fence);

  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &g.semDone;
  pi.swapchainCount = 1;
  pi.pSwapchains = &g.swapchain;
  pi.pImageIndices = &imageIndex;
  vkQueuePresentKHR(ctx.queue(), &pi);

  vkWaitForFences(dev, 1, &g.fence, VK_TRUE, UINT64_MAX);
  vkFreeCommandBuffers(dev, ctx.commandPool(), 1, &cmd);
}

} // namespace

// Offscreen test of the blit path (no window): renders the selected cache
// level's depth map through the blit shaders into a color target and checks
// the pixels actually vary. Exposed as `--selftest-blit`.
int runBlitSelftest() {
  if (!g.analyzer.init(false)) {
    std::fprintf(stderr, "Vulkan init failed\n");
    return 1;
  }
  VkContext& ctx = g.analyzer.context();
  VkDevice dev = ctx.device();
  ShadowCacheSystem& cache = g.analyzer.cache();
  for (size_t li = 0; li < cache.lightCount(); ++li)
    for (size_t ci = 0; ci < cache.cascadeCount(li); ++ci)
      cache.refresh(g.analyzer.scene(), li, ci);

  g.extent = {128, 128};
  g.swapFormat = VK_FORMAT_R8G8B8A8_UNORM;
  if (!createBlitPipeline()) {
    std::fprintf(stderr, "blit pipeline setup failed\n");
    return 1;
  }
  // No swapchain framebuffers in this test; make one offscreen target.
  GpuImage color;
  if (!createImage(ctx, 128, 128, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                   VK_IMAGE_ASPECT_COLOR_BIT, color))
    return 1;
  VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fci.renderPass = g.renderPass;
  fci.attachmentCount = 1;
  fci.pAttachments = &color.view;
  fci.width = 128;
  fci.height = 128;
  fci.layers = 1;
  VkFramebuffer fb;
  if (vkCreateFramebuffer(dev, &fci, nullptr, &fb) != VK_SUCCESS) return 1;

  CacheEntry& e = cache.entry(0, 0); // sun/c0
  VkCommandBuffer setup = ctx.beginCommands();
  transitionImage(setup, e.depth.image, e.layout,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_ASPECT_DEPTH_BIT);
  ctx.endCommands(setup);
  e.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkDescriptorImageInfo imgInfo{};
  imgInfo.sampler = g.sampler;
  imgInfo.imageView = e.depth.view;
  imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = g.descSet;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &imgInfo;
  vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

  GpuBuffer rb;
  createBuffer(ctx, 128 * 128 * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               rb);

  VkCommandBuffer cmd = ctx.beginCommands();
  VkClearValue clear{};
  clear.color = {{0, 0, 0, 1}};
  VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rbi.renderPass = g.renderPass;
  rbi.framebuffer = fb;
  rbi.renderArea = {{0, 0}, {128, 128}};
  rbi.clearValueCount = 1;
  rbi.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
  VkViewport vp{0, 0, 128, 128, 0, 1};
  VkRect2D sc{{0, 0}, {128, 128}};
  vkCmdSetViewport(cmd, 0, 1, &vp);
  vkCmdSetScissor(cmd, 0, 1, &sc);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeLayout,
                          0, 1, &g.descSet, 0, nullptr);
  vkCmdDraw(cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(cmd);

  VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  bar.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // renderPass finalLayout
  bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bar.image = color.image;
  bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &bar);
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {128, 128, 1};
  vkCmdCopyImageToBuffer(cmd, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         rb.buffer, 1, &copy);
  ctx.endCommands(cmd);

  const uint8_t* px = (const uint8_t*)rb.mapped;
  int mn = 255, mx = 0, nonzero = 0;
  for (int i = 0; i < 128 * 128; ++i) {
    int v = px[i * 4];
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    if (v > 0) ++nonzero;
  }
  std::printf("blit selftest: gray min=%d max=%d nonzeroPx=%d/16384\n", mn, mx,
              nonzero);
  // The ground plane fills the view, so no pure-white background; what
  // matters is that geometry produces a range of gray values.
  bool ok = (mx - mn) > 20 && nonzero > 10000;
  std::printf("BLIT %s\n", ok ? "PASS" : "FAIL");

  rb.destroy(dev);
  vkDestroyFramebuffer(dev, fb, nullptr);
  color.destroy(dev);
  vkDestroySampler(dev, g.sampler, nullptr);
  vkDestroyDescriptorPool(dev, g.descPool, nullptr);
  vkDestroyPipeline(dev, g.pipeline, nullptr);
  vkDestroyPipelineLayout(dev, g.pipeLayout, nullptr);
  vkDestroyDescriptorSetLayout(dev, g.setLayout, nullptr);
  vkDestroyRenderPass(dev, g.renderPass, nullptr);
  g.analyzer.shutdown();
  return ok ? 0 : 2;
}

int runInteractive() {
  if (!glfwInit()) {
    std::fprintf(stderr, "glfwInit failed\n");
    return 1;
  }
  uint32_t glfwExtCount = 0;
  const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
  std::vector<const char*> instExts(glfwExts, glfwExts + glfwExtCount);
  if (!g.analyzer.init(false, instExts)) {
    std::fprintf(stderr, "Vulkan init failed\n");
    return 1;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  g.window = glfwCreateWindow(900, 900, "shadow-cache analyzer", nullptr,
                              nullptr);
  if (!g.window) {
    std::fprintf(stderr, "window creation failed\n");
    return 1;
  }
  VkInstance instance = g.analyzer.context().instance();
  if (glfwCreateWindowSurface(instance, g.window, nullptr, &g.surface) !=
      VK_SUCCESS) {
    std::fprintf(stderr, "surface creation failed\n");
    return 1;
  }
  VkBool32 present = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(g.analyzer.context().physicalDevice(),
                                       g.analyzer.context().queueFamily(),
                                       g.surface, &present);
  if (!present) {
    std::fprintf(stderr, "queue family cannot present\n");
    return 1;
  }
  if (!createSwapchain() || !createBlitPipeline()) {
    std::fprintf(stderr, "swapchain/pipeline setup failed\n");
    return 1;
  }
  VkDevice dev = g.analyzer.context().device();
  VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  vkCreateSemaphore(dev, &sci, nullptr, &g.semAcquire);
  vkCreateSemaphore(dev, &sci, nullptr, &g.semDone);
  VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  vkCreateFence(dev, &fci, nullptr, &g.fence);

  glfwSetKeyCallback(g.window, onKey);
  std::printf(
      "viewer controls: TAB=light C=cascade R=refresh+compare "
      "1/2/3=scenario SPACE=animate ESC=quit\n");
  std::fflush(stdout);

  while (!glfwWindowShouldClose(g.window)) {
    glfwPollEvents();
    clampSelection();
    if (g.animate) {
      applyScenario(g.scenario, g.analyzer.scene(), g.frame++);
      // Cached policy: refresh only invalidated levels.
      ShadowCacheSystem& cache = g.analyzer.cache();
      for (size_t li = 0; li < cache.lightCount(); ++li)
        for (size_t ci = 0; ci < cache.cascadeCount(li); ++ci) {
          ReasonDetail det = cache.computeReasons(g.analyzer.scene(), li, ci);
          if (det.bits != ReasonNone) cache.refresh(g.analyzer.scene(), li, ci);
        }
    } else {
      // Ensure every level has content at least once.
      ShadowCacheSystem& cache = g.analyzer.cache();
      for (size_t li = 0; li < cache.lightCount(); ++li)
        for (size_t ci = 0; ci < cache.cascadeCount(li); ++ci)
          if (!cache.entry(li, ci).rendered)
            cache.refresh(g.analyzer.scene(), li, ci);
    }
    updateTitle();
    drawFrame();
  }

  vkDeviceWaitIdle(dev);
  vkDestroyFence(dev, g.fence, nullptr);
  vkDestroySemaphore(dev, g.semAcquire, nullptr);
  vkDestroySemaphore(dev, g.semDone, nullptr);
  vkDestroySampler(dev, g.sampler, nullptr);
  vkDestroyDescriptorPool(dev, g.descPool, nullptr);
  vkDestroyPipeline(dev, g.pipeline, nullptr);
  vkDestroyPipelineLayout(dev, g.pipeLayout, nullptr);
  vkDestroyDescriptorSetLayout(dev, g.setLayout, nullptr);
  for (auto fb : g.framebuffers) vkDestroyFramebuffer(dev, fb, nullptr);
  for (auto v : g.views) vkDestroyImageView(dev, v, nullptr);
  vkDestroyRenderPass(dev, g.renderPass, nullptr);
  vkDestroySwapchainKHR(dev, g.swapchain, nullptr);
  vkDestroySurfaceKHR(instance, g.surface, nullptr);
  g.analyzer.shutdown();
  glfwDestroyWindow(g.window);
  glfwTerminate();
  return 0;
}
