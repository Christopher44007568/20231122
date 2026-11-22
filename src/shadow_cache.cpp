#include "shadow_cache.h"

#include "spirv_gen.h"

#include <chrono>
#include <cmath>
#include <cstring>

const char* reasonName(uint32_t bit) {
  switch (bit) {
  case ReasonFirstRender: return "first-render";
  case ReasonLightTransform: return "light-transform";
  case ReasonCascadeConfig: return "cascade-config";
  case ReasonObjectTransform: return "object-transform";
  case ReasonMaterialChange: return "material-change";
  case ReasonManual: return "manual";
  default: return "?";
  }
}

static VkShaderModule makeModule(VkDevice dev,
                                 const std::vector<uint32_t>& spv) {
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = spv.size() * 4;
  ci.pCode = spv.data();
  VkShaderModule m = VK_NULL_HANDLE;
  vkCreateShaderModule(dev, &ci, nullptr, &m);
  return m;
}

bool ShadowCacheSystem::init(VkContext& ctx, const Scene& scene) {
  ctx_ = &ctx;
  VkDevice dev = ctx.device();

  // Shared unit-cube buffers.
  if (!createBuffer(ctx, 8 * 3 * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    vertexBuf_))
    return false;
  std::memcpy(vertexBuf_.mapped, Scene::cubeVertices(), 8 * 3 * 4);
  if (!createBuffer(ctx, 36 * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    indexBuf_))
    return false;
  std::memcpy(indexBuf_.mapped, Scene::cubeIndices(), 36 * 4);

  // Depth-only render pass.
  VkAttachmentDescription att{};
  att.format = VK_FORMAT_D32_SFLOAT;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.pDepthStencilAttachment = &ref;
  VkRenderPassCreateInfo rci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rci.attachmentCount = 1;
  rci.pAttachments = &att;
  rci.subpassCount = 1;
  rci.pSubpasses = &sub;
  if (vkCreateRenderPass(dev, &rci, nullptr, &renderPass_) != VK_SUCCESS)
    return false;

  if (!createPipeline()) return false;

  VkQueryPoolCreateInfo qci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qci.queryCount = 2;
  if (vkCreateQueryPool(dev, &qci, nullptr, &queryPool_) != VK_SUCCESS)
    queryPool_ = VK_NULL_HANDLE; // GPU timing optional

  // One cache entry per (light, cascade).
  lightCount_ = scene.lights.size();
  entries_.resize(lightCount_);
  resolutions_.resize(lightCount_);
  for (size_t li = 0; li < lightCount_; ++li) {
    const Light& light = scene.lights[li];
    uint32_t res = light.cascade.resolution;
    resolutions_[li] = res;
    for (size_t ci = 0; ci < light.cascade.splits.size(); ++ci) {
      CacheEntry e;
      if (!createImage(ctx, res, res, VK_FORMAT_D32_SFLOAT,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT,
                       VK_IMAGE_ASPECT_DEPTH_BIT, e.depth))
        return false;
      VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      fci.renderPass = renderPass_;
      fci.attachmentCount = 1;
      fci.pAttachments = &e.depth.view;
      fci.width = res;
      fci.height = res;
      fci.layers = 1;
      if (vkCreateFramebuffer(dev, &fci, nullptr, &e.framebuffer) != VK_SUCCESS)
        return false;
      entries_[li].push_back(e);
    }
  }
  return true;
}

bool ShadowCacheSystem::createPipeline() {
  VkDevice dev = ctx_->device();
  VkShaderModule vert = makeModule(dev, genDepthVert());
  VkShaderModule frag = makeModule(dev, genDepthFrag());
  if (!vert || !frag) return false;

  VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, 64};
  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &pcr;
  if (vkCreatePipelineLayout(dev, &plci, nullptr, &pipeLayout_) != VK_SUCCESS)
    return false;

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  VkVertexInputBindingDescription bind{0, 12, VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription attr{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
  VkPipelineVertexInputStateCreateInfo vin{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vin.vertexBindingDescriptionCount = 1;
  vin.pVertexBindingDescriptions = &bind;
  vin.vertexAttributeDescriptionCount = 1;
  vin.pVertexAttributeDescriptions = &attr;
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
  VkPipelineDepthStencilStateCreateInfo ds{
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkPipelineColorBlendStateCreateInfo cb{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
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
  pci.pDepthStencilState = &ds;
  pci.pColorBlendState = &cb;
  pci.pDynamicState = &dyn;
  pci.layout = pipeLayout_;
  pci.renderPass = renderPass_;
  bool ok = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr,
                                      &pipeline_) == VK_SUCCESS;
  vkDestroyShaderModule(dev, vert, nullptr);
  vkDestroyShaderModule(dev, frag, nullptr);
  return ok;
}

void ShadowCacheSystem::shutdown() {
  if (!ctx_) return;
  VkDevice dev = ctx_->device();
  vkDeviceWaitIdle(dev);
  for (auto& perLight : entries_)
    for (auto& e : perLight) {
      if (e.framebuffer)
        vkDestroyFramebuffer(dev, e.framebuffer, nullptr);
      e.depth.destroy(dev);
    }
  entries_.clear();
  if (queryPool_) vkDestroyQueryPool(dev, queryPool_, nullptr);
  if (pipeline_) vkDestroyPipeline(dev, pipeline_, nullptr);
  if (pipeLayout_) vkDestroyPipelineLayout(dev, pipeLayout_, nullptr);
  if (renderPass_) vkDestroyRenderPass(dev, renderPass_, nullptr);
  vertexBuf_.destroy(dev);
  indexBuf_.destroy(dev);
  ctx_ = nullptr;
}

Mat4 ShadowCacheSystem::cascadeViewProj(const Light& light,
                                        size_t cascade) const {
  Vec3 up{0, 1, 0};
  Vec3 dir = light.direction.normalized();
  if (std::fabs(dir.y) > 0.99f) up = {0, 0, 1};
  if (light.type == LightType::Directional) {
    float r = light.cascade.splits[cascade];
    float dist = r + 10.0f;
    Vec3 eye = light.focus - dir * dist;
    Mat4 view = Mat4::lookAt(eye, light.focus, up);
    Mat4 proj = Mat4::ortho(-r, r, -r, r, 0.1f, dist + r + 5.0f);
    return Mat4::multiply(proj, view);
  }
  // Spot: cascade = depth band [near, far] of the same frustum.
  float near = cascade == 0 ? 0.5f : light.cascade.splits[cascade - 1];
  float far = light.cascade.splits[cascade];
  Mat4 view = Mat4::lookAt(light.position, light.position + dir, up);
  float fovRad = light.fovDeg * 3.14159265f / 180.0f;
  Mat4 proj = Mat4::perspective(fovRad, 1.0f, near, far);
  return Mat4::multiply(proj, view);
}

bool ShadowCacheSystem::objectInCascade(const Light& light, size_t cascade,
                                        const SceneObject& obj) const {
  float rad = obj.boundingRadius();
  Vec3 dir = light.direction.normalized();
  if (light.type == LightType::Directional) {
    float r = light.cascade.splits[cascade];
    float dist = r + 10.0f;
    Vec3 eye = light.focus - dir * dist;
    Vec3 up{0, 1, 0};
    if (std::fabs(dir.y) > 0.99f) up = {0, 0, 1};
    Mat4 view = Mat4::lookAt(eye, light.focus, up);
    Vec3 c = view.transformPoint(obj.position);
    return std::fabs(c.x) <= r + rad && std::fabs(c.y) <= r + rad &&
           c.z >= 0.1f - rad && c.z <= dist + r + 5.0f + rad;
  }
  float near = cascade == 0 ? 0.5f : light.cascade.splits[cascade - 1];
  float far = light.cascade.splits[cascade];
  Vec3 toObj = obj.position - light.position;
  float distToObj = toObj.length();
  if (distToObj - rad > far || distToObj + rad < near) return false;
  if (distToObj < 1e-6f) return true;
  float cosAngle = toObj.normalized().dot(dir);
  float fovRad = light.fovDeg * 3.14159265f / 180.0f;
  float angularRad = std::asin(std::min(1.0f, rad / distToObj));
  return cosAngle >= std::cos(fovRad * 0.5f + angularRad);
}

ReasonDetail ShadowCacheSystem::computeReasons(const Scene& scene, size_t li,
                                               size_t ci) const {
  ReasonDetail d;
  const CacheEntry& e = entries_[li][ci];
  const Light& light = scene.lights[li];
  if (!e.rendered) {
    d.bits |= ReasonFirstRender;
    return d;
  }
  if (light.transformVersion != e.lightTransformVersion)
    d.bits |= ReasonLightTransform;
  if (light.cascadeVersion != e.lightCascadeVersion)
    d.bits |= ReasonCascadeConfig;

  // Diff the caster set and per-caster versions.
  std::unordered_map<uint32_t, uint32_t> currentCasters;
  for (const auto& obj : scene.objects) {
    if (!scene.materials[obj.materialId].castsShadow) continue;
    if (!objectInCascade(light, ci, obj)) continue;
    currentCasters[obj.id] = obj.transformVersion;
    auto it = e.casterVersions.find(obj.id);
    if (it == e.casterVersions.end() || it->second != obj.transformVersion) {
      d.bits |= ReasonObjectTransform;
      d.changedObjects.push_back(obj.id);
    }
    auto mit = e.materialVersions.find(obj.materialId);
    uint32_t mv = scene.materials[obj.materialId].version;
    if (mit == e.materialVersions.end() || mit->second != mv) {
      d.bits |= ReasonMaterialChange;
      d.changedMaterials.push_back(obj.materialId);
    }
  }
  for (const auto& kv : e.casterVersions)
    if (currentCasters.find(kv.first) == currentCasters.end()) {
      d.bits |= ReasonObjectTransform; // caster left the cascade
      d.changedObjects.push_back(kv.first);
    }
  return d;
}

bool ShadowCacheSystem::refresh(Scene& scene, size_t li, size_t ci,
                                uint32_t /*extraReason*/) {
  CacheEntry& e = entries_[li][ci];
  const Light& light = scene.lights[li];
  uint32_t res = resolutions_[li];
  e.viewProj = cascadeViewProj(light, ci);

  auto t0 = std::chrono::steady_clock::now();
  VkCommandBuffer cmd = ctx_->beginCommands();
  if (queryPool_) {
    vkCmdResetQueryPool(cmd, queryPool_, 0, 2);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool_, 0);
  }
  if (e.layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    transitionImage(cmd, e.depth.image, e.layout,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
    e.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }

  VkClearValue clear{};
  clear.depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rbi.renderPass = renderPass_;
  rbi.framebuffer = e.framebuffer;
  rbi.renderArea = {{0, 0}, {res, res}};
  rbi.clearValueCount = 1;
  rbi.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
  VkViewport viewport{0, 0, (float)res, (float)res, 0, 1};
  VkRect2D scissor{{0, 0}, {res, res}};
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);
  VkDeviceSize off = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuf_.buffer, &off);
  vkCmdBindIndexBuffer(cmd, indexBuf_.buffer, 0, VK_INDEX_TYPE_UINT32);

  std::unordered_map<uint32_t, uint32_t> casters;
  std::unordered_map<int, uint32_t> mats;
  for (const auto& obj : scene.objects) {
    if (!scene.materials[obj.materialId].castsShadow) continue;
    if (!objectInCascade(light, ci, obj)) continue;
    Mat4 mvp = Mat4::multiply(e.viewProj, obj.modelMatrix());
    vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 64,
                       mvp.m);
    vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);
    casters[obj.id] = obj.transformVersion;
    mats[obj.materialId] = scene.materials[obj.materialId].version;
  }
  vkCmdEndRenderPass(cmd);
  if (queryPool_)
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool_,
                        1);
  ctx_->endCommands(cmd);
  auto t1 = std::chrono::steady_clock::now();

  e.lastCpuMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
  e.lastGpuMs = 0.0;
  if (queryPool_ && ctx_->timestampsSupported()) {
    uint64_t ts[2] = {0, 0};
    if (vkGetQueryPoolResults(ctx_->device(), queryPool_, 0, 2, sizeof(ts), ts,
                              sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
      e.lastGpuMs =
          (double)(ts[1] - ts[0]) * ctx_->timestampPeriodNs() / 1.0e6;
  }
  e.rendered = true;
  e.lightTransformVersion = light.transformVersion;
  e.lightCascadeVersion = light.cascadeVersion;
  e.casterVersions = std::move(casters);
  e.materialVersions = std::move(mats);
  e.refreshCount++;
  e.totalCpuMs += e.lastCpuMs;
  e.totalGpuMs += e.lastGpuMs;
  return true;
}

bool ShadowCacheSystem::readback(size_t li, size_t ci,
                                 std::vector<float>& out) {
  CacheEntry& e = entries_[li][ci];
  uint32_t res = resolutions_[li];
  GpuBuffer rb;
  if (!createBuffer(*ctx_, (VkDeviceSize)res * res * 4,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    rb))
    return false;
  VkCommandBuffer cmd = ctx_->beginCommands();
  transitionImage(cmd, e.depth.image, e.layout,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_ASPECT_DEPTH_BIT);
  e.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
  copy.imageExtent = {res, res, 1};
  vkCmdCopyImageToBuffer(cmd, e.depth.image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.buffer, 1,
                         &copy);
  ctx_->endCommands(cmd);
  out.resize((size_t)res * res);
  std::memcpy(out.data(), rb.mapped, (size_t)res * res * 4);
  rb.destroy(ctx_->device());
  return true;
}
