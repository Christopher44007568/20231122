#include "viewer.h"
#include "cloud.h"
#include "util.h"
#include "vk.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace pcd {

void view_run(const Workspace& ws, const std::string& run_id) {
  std::string path = (run_id == "original") ? ws.original_bin()
                                            : ws.run_output(run_id);
  if (!file_exists(path)) die("nothing to view for: " + run_id);
  Cloud cloud = load_cloud(path);

  if (!glfwInit()) die("glfwInit failed (no display?)");
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* win = glfwCreateWindow(1024, 768,
      ("pcdclip - " + run_id).c_str(), nullptr, nullptr);
  if (!win) die("glfwCreateWindow failed");

  VkContext ctx = vk_create(true);
  VkSurfaceKHR surface;
  VK_CHECK(glfwCreateWindowSurface(ctx.instance, win, nullptr,
                                   &surface));

  // Swapchain.
  uint32_t nfmt = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.phys, surface, &nfmt,
                                       nullptr);
  std::vector<VkSurfaceFormatKHR> fmts(nfmt);
  vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.phys, surface, &nfmt,
                                       fmts.data());
  VkSurfaceFormatKHR chosen = fmts[0];
  for (auto f : fmts)
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM) chosen = f;
  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.phys, surface, &caps);
  VkExtent2D extent = caps.currentExtent;
  uint32_t img_count = caps.minImageCount + 1;
  if (caps.maxImageCount > 0 && img_count > caps.maxImageCount)
    img_count = caps.maxImageCount;
  VkSwapchainCreateInfoKHR sci{
      VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  sci.surface = surface;
  sci.minImageCount = img_count;
  sci.imageFormat = chosen.format;
  sci.imageColorSpace = chosen.colorSpace;
  sci.imageExtent = extent;
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  sci.clipped = VK_TRUE;
  VkSwapchainKHR swapchain;
  const char* dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  {
    // Recreate the device with the swapchain extension enabled.
    vkDestroyCommandPool(ctx.device, ctx.cmd_pool, nullptr);
    vkDestroyDevice(ctx.device, nullptr);
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = ctx.queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dev_exts;
    VK_CHECK(vkCreateDevice(ctx.phys, &dci, nullptr, &ctx.device));
    vkGetDeviceQueue(ctx.device, ctx.queue_family, 0, &ctx.queue);
    VkCommandPoolCreateInfo cpi{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.queueFamilyIndex = ctx.queue_family;
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(ctx.device, &cpi, nullptr,
                                 &ctx.cmd_pool));
  }
  VK_CHECK(vkCreateSwapchainKHR(ctx.device, &sci, nullptr, &swapchain));
  uint32_t nimg = 0;
  vkGetSwapchainImagesKHR(ctx.device, swapchain, &nimg, nullptr);
  std::vector<VkImage> images(nimg);
  vkGetSwapchainImagesKHR(ctx.device, swapchain, &nimg, images.data());
  std::vector<VkImageView> views(nimg);
  for (uint32_t i = 0; i < nimg; ++i) {
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = images[i];
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = chosen.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(ctx.device, &vci, nullptr, &views[i]));
  }

  VkAttachmentDescription att{};
  att.format = chosen.format;
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
  VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpi.attachmentCount = 1;
  rpi.pAttachments = &att;
  rpi.subpassCount = 1;
  rpi.pSubpasses = &sub;
  VkRenderPass rp;
  VK_CHECK(vkCreateRenderPass(ctx.device, &rpi, nullptr, &rp));
  std::vector<VkFramebuffer> fbs(nimg);
  for (uint32_t i = 0; i < nimg; ++i) {
    VkFramebufferCreateInfo fbi{
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbi.renderPass = rp;
    fbi.attachmentCount = 1;
    fbi.pAttachments = &views[i];
    fbi.width = extent.width;
    fbi.height = extent.height;
    fbi.layers = 1;
    VK_CHECK(vkCreateFramebuffer(ctx.device, &fbi, nullptr, &fbs[i]));
  }

  std::string dir = PCDCLIP_SHADER_DIR;
  VkShaderModule vs = vk_shader(ctx, dir + "/points.vert.spv");
  VkShaderModule fs = vk_shader(ctx, dir + "/points.frag.spv");
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = "main";
  stages[1] = stages[0];
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs;
  VkVertexInputBindingDescription vb{0, sizeof(Point),
                                     VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription va[2]{};
  va[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
  va[1] = {1, 0, VK_FORMAT_R32_UINT, 12};
  VkPipelineVertexInputStateCreateInfo vi{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &vb;
  vi.vertexAttributeDescriptionCount = 2;
  vi.pVertexAttributeDescriptions = va;
  VkPipelineInputAssemblyStateCreateInfo ia{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0, 1};
  VkRect2D sc{{0, 0}, extent};
  VkPipelineViewportStateCreateInfo vps{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vps.viewportCount = 1;
  vps.pViewports = &vp;
  vps.scissorCount = 1;
  vps.pScissors = &sc;
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
  cba.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo cb{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;
  VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, 64};
  VkPipelineLayoutCreateInfo pli{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges = &pcr;
  VkPipelineLayout playout;
  VK_CHECK(vkCreatePipelineLayout(ctx.device, &pli, nullptr, &playout));
  VkGraphicsPipelineCreateInfo gp{
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gp.stageCount = 2;
  gp.pStages = stages;
  gp.pVertexInputState = &vi;
  gp.pInputAssemblyState = &ia;
  gp.pViewportState = &vps;
  gp.pRasterizationState = &rs;
  gp.pMultisampleState = &ms;
  gp.pColorBlendState = &cb;
  gp.layout = playout;
  gp.renderPass = rp;
  VkPipeline pipeline;
  VK_CHECK(vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &gp,
                                     nullptr, &pipeline));
  vkDestroyShaderModule(ctx.device, vs, nullptr);
  vkDestroyShaderModule(ctx.device, fs, nullptr);

  VkDeviceSize vbytes = cloud.points.size() * sizeof(Point);
  VkBuffer2 vbuf = vk_buffer(ctx, std::max<VkDeviceSize>(vbytes, 16),
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
  if (vbytes > 0) vk_upload(ctx, vbuf, cloud.points.data(), vbytes);

  VkSemaphoreCreateInfo semci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore sem_acq, sem_done;
  VK_CHECK(vkCreateSemaphore(ctx.device, &semci, nullptr, &sem_acq));
  VK_CHECK(vkCreateSemaphore(ctx.device, &semci, nullptr, &sem_done));
  VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  VkFence fence;
  VK_CHECK(vkCreateFence(ctx.device, &fci, nullptr, &fence));

  // Precompute cloud center/scale for the per-frame MVP.
  float lo[3], hi[3];
  cloud_bounds(cloud, lo, hi);
  float cx = (lo[0] + hi[0]) / 2, cy = (lo[1] + hi[1]) / 2,
        cz = (lo[2] + hi[2]) / 2;
  float ext = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
  if (ext <= 0) ext = 1;
  float s = 1.7f / ext;

  std::printf("viewer: ESC to quit; rotating view of %s (%zu points)\n",
              run_id.c_str(), cloud.points.size());
  while (!glfwWindowShouldClose(win)) {
    glfwPollEvents();
    if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
    VK_CHECK(vkWaitForFences(ctx.device, 1, &fence, VK_TRUE,
                             UINT64_MAX));
    VK_CHECK(vkResetFences(ctx.device, 1, &fence));
    uint32_t idx = 0;
    VkResult acq = vkAcquireNextImageKHR(ctx.device, swapchain,
                                         UINT64_MAX, sem_acq,
                                         VK_NULL_HANDLE, &idx);
    if (acq != VK_SUCCESS) continue;

    float ang = (float)glfwGetTime() * 0.5f;
    float aspect = (float)extent.width / (float)extent.height;
    float mvp[16] = {0};
    float ca = std::cos(ang), sa = std::sin(ang);
    float tilt = 0.6f, ct = std::cos(tilt), st = std::sin(tilt);
    // scale * tilt * rotY, translation folded in.
    float m00 = s / aspect * ca, m02 = s / aspect * sa;
    float m10 = -s * st * sa, m11 = s * ct, m12 = s * st * ca;
    float m20 = s * ct * sa, m21 = s * st, m22 = -s * ct * ca;
    mvp[0] = m00; mvp[8] = m02;
    mvp[1] = m10; mvp[5] = m11; mvp[9] = m12;
    mvp[2] = m20; mvp[6] = m21; mvp[10] = m22;
    mvp[15] = 1.0f;
    mvp[12] = -(mvp[0] * cx + mvp[4] * cy + mvp[8] * cz);
    mvp[13] = -(mvp[1] * cx + mvp[5] * cy + mvp[9] * cz);
    mvp[14] = -(mvp[2] * cx + mvp[6] * cy + mvp[10] * cz);

    VkCommandBufferAllocateInfo cai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = ctx.cmd_pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(ctx.device, &cai, &cmd));
    VkCommandBufferBeginInfo bi{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    VkClearValue clear{};
    clear.color = {{0.02f, 0.02f, 0.03f, 1.0f}};
    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass = rp;
    rbi.framebuffer = fbs[idx];
    rbi.renderArea = {{0, 0}, extent};
    rbi.clearValueCount = 1;
    rbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdPushConstants(cmd, playout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64,
                       mvp);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf.buf, &off);
    vkCmdDraw(cmd, (uint32_t)cloud.points.size(), 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkPipelineStageFlags wait =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &sem_acq;
    si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &sem_done;
    VK_CHECK(vkQueueSubmit(ctx.queue, 1, &si, fence));
    VkPresentInfoKHR pri{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pri.waitSemaphoreCount = 1;
    pri.pWaitSemaphores = &sem_done;
    pri.swapchainCount = 1;
    pri.pSwapchains = &swapchain;
    pri.pImageIndices = &idx;
    vkQueuePresentKHR(ctx.queue, &pri);
    vkFreeCommandBuffers(ctx.device, ctx.cmd_pool, 1, &cmd);
  }

  vkDeviceWaitIdle(ctx.device);
  vkDestroyFence(ctx.device, fence, nullptr);
  vkDestroySemaphore(ctx.device, sem_acq, nullptr);
  vkDestroySemaphore(ctx.device, sem_done, nullptr);
  vk_buffer_destroy(ctx, vbuf);
  vkDestroyPipeline(ctx.device, pipeline, nullptr);
  vkDestroyPipelineLayout(ctx.device, playout, nullptr);
  for (auto fb : fbs) vkDestroyFramebuffer(ctx.device, fb, nullptr);
  vkDestroyRenderPass(ctx.device, rp, nullptr);
  for (auto v : views) vkDestroyImageView(ctx.device, v, nullptr);
  vkDestroySwapchainKHR(ctx.device, swapchain, nullptr);
  vkDestroySurfaceKHR(ctx.instance, surface, nullptr);
  vk_destroy(ctx);
  glfwDestroyWindow(win);
  glfwTerminate();
}

} // namespace pcd
