#include "render.h"
#include "util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace pcd {

// Column-major 4x4.
static void mat4_identity(float m[16]) {
  std::memset(m, 0, 64);
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_mul(float out[16], const float a[16], const float b[16]) {
  float r[16];
  for (int c = 0; c < 4; ++c)
    for (int row = 0; row < 4; ++row) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a[k * 4 + row] * b[c * 4 + k];
      r[c * 4 + row] = s;
    }
  std::memcpy(out, r, 64);
}

// Orthographic MVP: center the cloud, uniform scale, isometric tilt,
// y flipped for top-down image rows.
static void make_mvp(const Cloud& cloud, float aspect, float m[16]) {
  float lo[3], hi[3];
  cloud_bounds(cloud, lo, hi);
  float cx = (lo[0] + hi[0]) / 2, cy = (lo[1] + hi[1]) / 2,
        cz = (lo[2] + hi[2]) / 2;
  float ext = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
  if (ext <= 0) ext = 1;
  float s = 1.7f / ext;

  float rot_y[16], rot_x[16], view[16];
  mat4_identity(rot_y);
  float ay = 0.6154797f; // ~35.26 deg, classic isometric
  rot_y[0] = std::cos(ay);  rot_y[8] = std::sin(ay);
  rot_y[2] = -std::sin(ay); rot_y[10] = std::cos(ay);
  mat4_identity(rot_x);
  float ax = 0.7853982f; // 45 deg
  rot_x[5] = std::cos(ax);  rot_x[9] = -std::sin(ax);
  rot_x[6] = std::sin(ax);  rot_x[10] = std::cos(ax);
  mat4_mul(view, rot_x, rot_y);

  float scale[16];
  mat4_identity(scale);
  scale[0] = s / aspect;
  scale[5] = -s; // flip y for image rows
  scale[10] = s;

  float mvp[16];
  mat4_mul(mvp, scale, view);
  // Translation to cloud center, folded into the matrix.
  float tx = -(mvp[0] * cx + mvp[4] * cy + mvp[8] * cz);
  float ty = -(mvp[1] * cx + mvp[5] * cy + mvp[9] * cz);
  float tz = -(mvp[2] * cx + mvp[6] * cy + mvp[10] * cz);
  mvp[12] = tx; mvp[13] = ty; mvp[14] = tz;
  std::memcpy(m, mvp, 64);
}

struct Image2 {
  VkImage img = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

static Image2 make_image(VkContext& ctx, uint32_t w, uint32_t h,
                         VkFormat fmt, VkImageUsageFlags usage) {
  Image2 im;
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = fmt;
  ici.extent = {w, h, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = usage;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VK_CHECK(vkCreateImage(ctx.device, &ici, nullptr, &im.img));
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(ctx.device, im.img, &req);
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(ctx.phys, &mp);
  uint32_t type = UINT32_MAX;
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    if ((req.memoryTypeBits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags &
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      type = i;
  if (type == UINT32_MAX) type = 0;
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = type;
  VK_CHECK(vkAllocateMemory(ctx.device, &mai, nullptr, &im.mem));
  VK_CHECK(vkBindImageMemory(ctx.device, im.img, im.mem, 0));
  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = im.img;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = fmt;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VK_CHECK(vkCreateImageView(ctx.device, &vci, nullptr, &im.view));
  return im;
}

void render_snapshot(VkContext& ctx, const Cloud& cloud,
                     const std::string& ppm_path, uint32_t width,
                     uint32_t height) {
  const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
  Image2 color = make_image(ctx, width, height, fmt,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

  VkAttachmentDescription att{};
  att.format = fmt;
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
  VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpi.attachmentCount = 1;
  rpi.pAttachments = &att;
  rpi.subpassCount = 1;
  rpi.pSubpasses = &sub;
  VkRenderPass rp;
  VK_CHECK(vkCreateRenderPass(ctx.device, &rpi, nullptr, &rp));

  VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fbi.renderPass = rp;
  fbi.attachmentCount = 1;
  fbi.pAttachments = &color.view;
  fbi.width = width;
  fbi.height = height;
  fbi.layers = 1;
  VkFramebuffer fb;
  VK_CHECK(vkCreateFramebuffer(ctx.device, &fbi, nullptr, &fb));

  // Graphics pipeline: points with per-point clip flag coloring.
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
  VkViewport vp{0, 0, (float)width, (float)height, 0, 1};
  VkRect2D sc{{0, 0}, {width, height}};
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
  cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                       VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT |
                       VK_COLOR_COMPONENT_A_BIT;
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
  VkDeviceSize img_bytes = VkDeviceSize(width) * height * 4;
  VkBuffer2 readback = vk_buffer(ctx, img_bytes,
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);

  float mvp[16];
  make_mvp(cloud, float(width) / float(height), mvp);

  vk_one_shot(ctx, [&](VkCommandBuffer cmd) {
    VkClearValue clear{};
    clear.color = {{0.02f, 0.02f, 0.03f, 1.0f}};
    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass = rp;
    rbi.framebuffer = fb;
    rbi.renderArea = {{0, 0}, {width, height}};
    rbi.clearValueCount = 1;
    rbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdPushConstants(cmd, playout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64,
                       mvp);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf.buf, &off);
    vkCmdDraw(cmd, static_cast<uint32_t>(cloud.points.size()), 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cmd, color.img,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback.buf, 1, &region);
  });

  std::vector<uint8_t> px(static_cast<size_t>(img_bytes));
  vk_download(ctx, readback, px.data(), img_bytes);
  std::string header = "P6\n" + std::to_string(width) + " " +
                       std::to_string(height) + "\n255\n";
  FILE* f = std::fopen(ppm_path.c_str(), "wb");
  if (!f) die("cannot write: " + ppm_path);
  std::fwrite(header.data(), 1, header.size(), f);
  for (uint32_t i = 0; i < width * height; ++i) {
    uint8_t rgb[3] = {px[i * 4 + 0], px[i * 4 + 1], px[i * 4 + 2]};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);

  vk_buffer_destroy(ctx, vbuf);
  vk_buffer_destroy(ctx, readback);
  vkDestroyPipeline(ctx.device, pipeline, nullptr);
  vkDestroyPipelineLayout(ctx.device, playout, nullptr);
  vkDestroyFramebuffer(ctx.device, fb, nullptr);
  vkDestroyRenderPass(ctx.device, rp, nullptr);
  vkDestroyImageView(ctx.device, color.view, nullptr);
  vkDestroyImage(ctx.device, color.img, nullptr);
  vkFreeMemory(ctx.device, color.mem, nullptr);
}

} // namespace pcd
