#include "renderer.h"
#include <cmath>
#include <cstring>

namespace {

struct Vertex {
  float pos[3];
  float normal[3];
  float color[3];
};

// Deterministic LCG for scene generation.
struct Lcg {
  uint32_t s;
  uint32_t next() {
    s = s * 1664525u + 1013904223u;
    return s;
  }
  float range(float lo, float hi) {
    return lo + (hi - lo) * float(next() % 10000) / 10000.0f;
  }
};

void addQuad(std::vector<Vertex>& out, const Vec3& a, const Vec3& b,
             const Vec3& c, const Vec3& d, const Vec3& n, const Vec3& col) {
  auto push = [&](const Vec3& p) {
    out.push_back({{p.x, p.y, p.z}, {n.x, n.y, n.z}, {col.x, col.y, col.z}});
  };
  push(a); push(b); push(c);
  push(a); push(c); push(d);
}

void addBox(std::vector<Vertex>& out, const Vec3& center, const Vec3& half,
            float rotY, const Vec3& col) {
  Mat4 rot = Mat4::rotationY(rotY);
  auto xf = [&](float x, float y, float z) {
    Vec4 v = rot * Vec4{x, y, z, 1.0f};
    return Vec3{v.x + center.x, v.y + center.y, v.z + center.z};
  };
  auto xn = [&](float x, float y, float z) {
    Vec4 v = rot * Vec4{x, y, z, 0.0f};
    return Vec3{v.x, v.y, v.z}.normalized();
  };
  const float hx = half.x, hy = half.y, hz = half.z;
  // +Y / -Y
  addQuad(out, xf(-hx, hy, -hz), xf(-hx, hy, hz), xf(hx, hy, hz),
          xf(hx, hy, -hz), xn(0, 1, 0), col);
  addQuad(out, xf(-hx, -hy, -hz), xf(hx, -hy, -hz), xf(hx, -hy, hz),
          xf(-hx, -hy, hz), xn(0, -1, 0), col);
  // +X / -X
  addQuad(out, xf(hx, -hy, -hz), xf(hx, -hy, hz), xf(hx, hy, hz),
          xf(hx, hy, -hz), xn(1, 0, 0), col);
  addQuad(out, xf(-hx, -hy, -hz), xf(-hx, -hy, hz), xf(-hx, hy, hz),
          xf(-hx, hy, -hz), xn(-1, 0, 0), col);
  // +Z / -Z
  addQuad(out, xf(-hx, -hy, hz), xf(hx, -hy, hz), xf(hx, hy, hz),
          xf(-hx, hy, hz), xn(0, 0, 1), col);
  addQuad(out, xf(-hx, -hy, -hz), xf(hx, -hy, -hz), xf(hx, hy, -hz),
          xf(-hx, hy, -hz), xn(0, 0, -1), col);
}

} // namespace

bool CsmRenderer::createGeometry(uint32_t seed, std::string& err) {
  std::vector<Vertex> verts;

  // Ground plane.
  const float g = 90.0f;
  addQuad(verts, {-g, 0, -g}, {-g, 0, g}, {g, 0, g}, {g, 0, -g},
          {0, 1, 0}, {0.45f, 0.5f, 0.42f});

  Lcg lcg{seed};
  const Vec3 palette[5] = {
      {0.75f, 0.30f, 0.25f}, {0.25f, 0.55f, 0.75f}, {0.85f, 0.70f, 0.30f},
      {0.45f, 0.35f, 0.70f}, {0.35f, 0.70f, 0.45f}};
  const int boxCount = 24;
  for (int i = 0; i < boxCount; ++i) {
    float x = lcg.range(-55.0f, 55.0f);
    float z = lcg.range(-55.0f, 55.0f);
    float hx = lcg.range(0.8f, 3.0f);
    float hy = lcg.range(0.8f, 5.0f);
    float hz = lcg.range(0.8f, 3.0f);
    float rot = lcg.range(0.0f, 3.14159f);
    Vec3 col = palette[lcg.next() % 5];
    addBox(verts, {x, hy, z}, {hx, hy, hz}, rot, col);
  }

  vertexCount_ = uint32_t(verts.size());
  VkDeviceSize size = VkDeviceSize(verts.size()) * sizeof(Vertex);
  GpuBuffer staging = createBuffer(*ctx_, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  vbuf_ = createBuffer(*ctx_, size,
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (staging.buf == VK_NULL_HANDLE || vbuf_.buf == VK_NULL_HANDLE) {
    err = "vertex buffer allocation failed";
    return false;
  }
  void* mapped = nullptr;
  vkMapMemory(ctx_->device, staging.mem, 0, size, 0, &mapped);
  std::memcpy(mapped, verts.data(), size_t(size));
  vkUnmapMemory(ctx_->device, staging.mem);

  VkCommandBuffer cmd = beginOneShot(*ctx_);
  VkBufferCopy copy{0, 0, size};
  vkCmdCopyBuffer(cmd, staging.buf, vbuf_.buf, 1, &copy);
  endOneShot(*ctx_, cmd);
  destroyBuffer(*ctx_, staging);
  return true;
}

bool CsmRenderer::createRenderPasses(std::string& err) {
  // Shadow pass: depth only, ends in SHADER_READ_ONLY for sampling.
  VkAttachmentDescription depthAtt{};
  depthAtt.format = VK_FORMAT_D32_SFLOAT;
  depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

  VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.pDepthStencilAttachment = &depthRef;

  VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpci.attachmentCount = 1;
  rpci.pAttachments = &depthAtt;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &sub;
  if (vkCreateRenderPass(ctx_->device, &rpci, nullptr, &shadowPass_) != VK_SUCCESS) {
    err = "shadow render pass creation failed";
    return false;
  }

  // Scene pass: color + depth, color ends SHADER_READ_ONLY for readback.
  VkAttachmentDescription atts[2]{};
  atts[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
  atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  atts[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  atts[1] = depthAtt;
  atts[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference sceneDepthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sceneSub{};
  sceneSub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sceneSub.colorAttachmentCount = 1;
  sceneSub.pColorAttachments = &colorRef;
  sceneSub.pDepthStencilAttachment = &sceneDepthRef;

  VkRenderPassCreateInfo sceneRpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  sceneRpci.attachmentCount = 2;
  sceneRpci.pAttachments = atts;
  sceneRpci.subpassCount = 1;
  sceneRpci.pSubpasses = &sceneSub;
  if (vkCreateRenderPass(ctx_->device, &sceneRpci, nullptr, &scenePass_) != VK_SUCCESS) {
    err = "scene render pass creation failed";
    return false;
  }
  return true;
}

bool CsmRenderer::createPipelines(std::string& err) {
  VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription attrs[3]{};
  attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
  attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};
  attrs[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24};

  VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vin.vertexBindingDescriptionCount = 1;
  vin.pVertexBindingDescriptions = &binding;
  vin.vertexAttributeDescriptionCount = 3;
  vin.pVertexAttributeDescriptions = attrs;

  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

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

  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS;

  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;

  VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dynStates;

  VkPushConstantRange pcRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4)};

  // --- Shadow pipeline (vertex only) ---
  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &pcRange;
  if (vkCreatePipelineLayout(ctx_->device, &plci, nullptr, &shadowPipeLayout_) != VK_SUCCESS) {
    err = "shadow pipeline layout failed";
    return false;
  }
  VkShaderModule shadowVert = loadShader(*ctx_, std::string(SPV_DIR) + "/shadow.vert.spv");
  if (shadowVert == VK_NULL_HANDLE) {
    err = "failed to load shadow.vert.spv";
    return false;
  }
  VkPipelineShaderStageCreateInfo shadowStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  shadowStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  shadowStage.module = shadowVert;
  shadowStage.pName = "main";

  VkPipelineRasterizationStateCreateInfo rsShadow = rs;
  rsShadow.depthBiasEnable = VK_TRUE;
  rsShadow.depthBiasConstantFactor = 1.5f;
  rsShadow.depthBiasSlopeFactor = 1.75f;

  VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gpci.stageCount = 1;
  gpci.pStages = &shadowStage;
  gpci.pVertexInputState = &vin;
  gpci.pInputAssemblyState = &ia;
  gpci.pViewportState = &vp;
  gpci.pRasterizationState = &rsShadow;
  gpci.pMultisampleState = &ms;
  gpci.pDepthStencilState = &ds;
  gpci.pDynamicState = &dyn;
  gpci.layout = shadowPipeLayout_;
  gpci.renderPass = shadowPass_;
  if (vkCreateGraphicsPipelines(ctx_->device, VK_NULL_HANDLE, 1, &gpci,
                                nullptr, &shadowPipe_) != VK_SUCCESS) {
    err = "shadow pipeline creation failed";
    vkDestroyShaderModule(ctx_->device, shadowVert, nullptr);
    return false;
  }
  vkDestroyShaderModule(ctx_->device, shadowVert, nullptr);

  // --- Scene pipeline (vertex + fragment, UBO + 3 shadow samplers) ---
  VkDescriptorSetLayoutBinding bindings[4]{};
  bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  for (int i = 1; i <= 3; ++i)
    bindings[i] = {uint32_t(i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dslci.bindingCount = 4;
  dslci.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(ctx_->device, &dslci, nullptr, &descLayout_) != VK_SUCCESS) {
    err = "descriptor set layout failed";
    return false;
  }

  VkPipelineLayoutCreateInfo scenePlci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  scenePlci.setLayoutCount = 1;
  scenePlci.pSetLayouts = &descLayout_;
  scenePlci.pushConstantRangeCount = 1;
  scenePlci.pPushConstantRanges = &pcRange;
  if (vkCreatePipelineLayout(ctx_->device, &scenePlci, nullptr, &scenePipeLayout_) != VK_SUCCESS) {
    err = "scene pipeline layout failed";
    return false;
  }

  VkShaderModule sceneVert = loadShader(*ctx_, std::string(SPV_DIR) + "/scene.vert.spv");
  VkShaderModule sceneFrag = loadShader(*ctx_, std::string(SPV_DIR) + "/scene.frag.spv");
  if (sceneVert == VK_NULL_HANDLE || sceneFrag == VK_NULL_HANDLE) {
    err = "failed to load scene shaders";
    return false;
  }
  VkPipelineShaderStageCreateInfo sceneStages[2]{};
  sceneStages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  sceneStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  sceneStages[0].module = sceneVert;
  sceneStages[0].pName = "main";
  sceneStages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  sceneStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  sceneStages[1].module = sceneFrag;
  sceneStages[1].pName = "main";

  VkGraphicsPipelineCreateInfo sceneGpci = gpci;
  sceneGpci.stageCount = 2;
  sceneGpci.pStages = sceneStages;
  sceneGpci.pRasterizationState = &rs;
  sceneGpci.pColorBlendState = &cb;
  sceneGpci.layout = scenePipeLayout_;
  sceneGpci.renderPass = scenePass_;
  VkResult pr = vkCreateGraphicsPipelines(ctx_->device, VK_NULL_HANDLE, 1,
                                          &sceneGpci, nullptr, &scenePipe_);
  vkDestroyShaderModule(ctx_->device, sceneVert, nullptr);
  vkDestroyShaderModule(ctx_->device, sceneFrag, nullptr);
  if (pr != VK_SUCCESS) {
    err = "scene pipeline creation failed";
    return false;
  }
  return true;
}

bool CsmRenderer::createFrameResources(const SceneParams& params,
                                       std::string& err) {
  width_ = params.width;
  height_ = params.height;
  shadowSize_ = params.shadowMapSize;

  for (int i = 0; i < 3; ++i) {
    shadowMaps_[i] = createImage2D(*ctx_, shadowSize_, shadowSize_,
                                   VK_FORMAT_D32_SFLOAT,
                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_SAMPLED_BIT,
                                   VK_IMAGE_ASPECT_DEPTH_BIT);
    if (shadowMaps_[i].img == VK_NULL_HANDLE) {
      err = "shadow map image creation failed";
      return false;
    }
  }
  colorTarget_ = createImage2D(*ctx_, width_, height_, VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_SAMPLED_BIT |
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT);
  depthTarget_ = createImage2D(*ctx_, width_, height_, VK_FORMAT_D32_SFLOAT,
                               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                               VK_IMAGE_ASPECT_DEPTH_BIT);
  if (colorTarget_.img == VK_NULL_HANDLE || depthTarget_.img == VK_NULL_HANDLE) {
    err = "render target creation failed";
    return false;
  }

  for (int i = 0; i < 3; ++i) {
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = shadowPass_;
    fci.attachmentCount = 1;
    fci.pAttachments = &shadowMaps_[i].view;
    fci.width = shadowSize_;
    fci.height = shadowSize_;
    fci.layers = 1;
    if (vkCreateFramebuffer(ctx_->device, &fci, nullptr, &shadowFb_[i]) != VK_SUCCESS) {
      err = "shadow framebuffer creation failed";
      return false;
    }
  }
  VkImageView sceneAtts[2] = {colorTarget_.view, depthTarget_.view};
  VkFramebufferCreateInfo sfci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  sfci.renderPass = scenePass_;
  sfci.attachmentCount = 2;
  sfci.pAttachments = sceneAtts;
  sfci.width = width_;
  sfci.height = height_;
  sfci.layers = 1;
  if (vkCreateFramebuffer(ctx_->device, &sfci, nullptr, &sceneFb_) != VK_SUCCESS) {
    err = "scene framebuffer creation failed";
    return false;
  }

  ubo_ = createBuffer(*ctx_, sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (ubo_.buf == VK_NULL_HANDLE) {
    err = "UBO allocation failed";
    return false;
  }

  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = VK_FILTER_LINEAR;
  sci.minFilter = VK_FILTER_LINEAR;
  sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.compareEnable = VK_TRUE;
  sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  if (vkCreateSampler(ctx_->device, &sci, nullptr, &shadowSampler_) != VK_SUCCESS) {
    err = "sampler creation failed";
    return false;
  }

  VkDescriptorPoolSize poolSizes[2]{};
  poolSizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
  poolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3};
  VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpci.maxSets = 1;
  dpci.poolSizeCount = 2;
  dpci.pPoolSizes = poolSizes;
  if (vkCreateDescriptorPool(ctx_->device, &dpci, nullptr, &descPool_) != VK_SUCCESS) {
    err = "descriptor pool creation failed";
    return false;
  }
  VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dsai.descriptorPool = descPool_;
  dsai.descriptorSetCount = 1;
  dsai.pSetLayouts = &descLayout_;
  if (vkAllocateDescriptorSets(ctx_->device, &dsai, &descSet_) != VK_SUCCESS) {
    err = "descriptor set allocation failed";
    return false;
  }

  VkDescriptorBufferInfo uboInfo{ubo_.buf, 0, sizeof(FrameUBO)};
  VkWriteDescriptorSet writes[4]{};
  writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  writes[0].dstSet = descSet_;
  writes[0].dstBinding = 0;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  writes[0].pBufferInfo = &uboInfo;
  VkDescriptorImageInfo imgInfos[3]{};
  for (int i = 0; i < 3; ++i) {
    imgInfos[i] = {shadowSampler_, shadowMaps_[i].view,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    writes[i + 1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[i + 1].dstSet = descSet_;
    writes[i + 1].dstBinding = uint32_t(i + 1);
    writes[i + 1].descriptorCount = 1;
    writes[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[i + 1].pImageInfo = &imgInfos[i];
  }
  vkUpdateDescriptorSets(ctx_->device, 4, writes, 0, nullptr);

  VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cbai.commandPool = ctx_->cmdPool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(ctx_->device, &cbai, &cmd_) != VK_SUCCESS) {
    err = "command buffer allocation failed";
    return false;
  }
  return true;
}

bool CsmRenderer::init(const VkContext& ctx, const SceneParams& params,
                       std::string& err) {
  ctx_ = &ctx;
  VkFormatProperties fp;
  vkGetPhysicalDeviceFormatProperties(ctx.phys, VK_FORMAT_D32_SFLOAT, &fp);
  VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  if ((fp.optimalTilingFeatures & need) != need) {
    err = "D32_SFLOAT lacks required features";
    return false;
  }
  if (!createGeometry(params.seed, err)) return false;
  if (!createRenderPasses(err)) return false;
  if (!createPipelines(err)) return false;
  if (!createFrameResources(params, err)) return false;
  initialized_ = true;
  return true;
}

void CsmRenderer::computeLightMatrices(const SceneParams& p, Mat4 outVP[3],
                                       float outSplits[3]) const {
  const float n = p.camNear, f = p.camFar;
  for (int i = 0; i < 3; ++i) {
    float t = float(i + 1) / 3.0f;
    float uni = n + (f - n) * t;
    float log = n * std::pow(f / n, t);
    outSplits[i] = p.splitLambda * log + (1.0f - p.splitLambda) * uni;
  }

  float yaw = p.camYawDeg * 3.14159265f / 180.0f;
  float pitch = p.camPitchDeg * 3.14159265f / 180.0f;
  Vec3 target{0.0f, 2.0f, 0.0f};
  Vec3 eye{target.x + p.camDist * std::cos(pitch) * std::cos(yaw),
           target.y + p.camDist * std::sin(pitch),
           target.z + p.camDist * std::cos(pitch) * std::sin(yaw)};
  Mat4 view = Mat4::lookAt(eye, target, {0, 1, 0});
  float aspect = float(p.width) / float(p.height);
  float fovY = p.camFovDeg * 3.14159265f / 180.0f;

  Vec3 L = p.lightDir.normalized();
  Vec3 upRef = std::fabs(L.y) > 0.95f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};

  for (int c = 0; c < 3; ++c) {
    float sliceNear = (c == 0) ? n : outSplits[c - 1];
    float sliceFar = outSplits[c];
    Mat4 sliceProj = Mat4::perspective(fovY, aspect, sliceNear, sliceFar);
    Mat4 invVP = (sliceProj * view).inverted();

    Vec3 corners[8];
    int idx = 0;
    for (int z = 0; z < 2; ++z)
      for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
          Vec4 ndc{x ? 1.0f : -1.0f, y ? 1.0f : -1.0f, z ? 1.0f : 0.0f, 1.0f};
          Vec4 w = invVP * ndc;
          corners[idx++] = {w.x / w.w, w.y / w.w, w.z / w.w};
        }

    Vec3 center{0, 0, 0};
    for (auto& cn : corners) center = center + cn;
    center = center * (1.0f / 8.0f);

    float radius = 0.0f;
    for (auto& cn : corners) {
      float d = (cn - center).length();
      if (d > radius) radius = d;
    }

    Vec3 lightEye = center - L * (radius * 2.0f + 10.0f);
    Mat4 lightView = Mat4::lookAt(lightEye, center, upRef);

    float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
    float minZ = 1e30f, maxZ = -1e30f;
    for (auto& cn : corners) {
      Vec4 v = lightView * Vec4{cn.x, cn.y, cn.z, 1.0f};
      minX = v.x < minX ? v.x : minX; maxX = v.x > maxX ? v.x : maxX;
      minY = v.y < minY ? v.y : minY; maxY = v.y > maxY ? v.y : maxY;
      minZ = v.z < minZ ? v.z : minZ; maxZ = v.z > maxZ ? v.z : maxZ;
    }
    // Expand z to cover casters outside the slice frustum.
    minZ -= 60.0f;

    // Snap xy bounds to shadow-map texels for stable edges.
    float texel = (maxX - minX) / float(p.shadowMapSize);
    if (texel > 0.0f) {
      minX = std::floor(minX / texel) * texel;
      maxX = std::floor(maxX / texel) * texel;
      minY = std::floor(minY / texel) * texel;
      maxY = std::floor(maxY / texel) * texel;
    }

    Mat4 lightProj = Mat4::ortho(minX, maxX, minY, maxY, -maxZ, -minZ);
    outVP[c] = lightProj * lightView;
  }
}

bool CsmRenderer::record(const RenderJob& job, std::string& err) {
  if (!initialized_) {
    err = "renderer not initialized";
    return false;
  }
  const SceneParams& p = job.params;

  Mat4 lightVP[3];
  float splits[3];
  computeLightMatrices(p, lightVP, splits);

  float yaw = p.camYawDeg * 3.14159265f / 180.0f;
  float pitch = p.camPitchDeg * 3.14159265f / 180.0f;
  Vec3 target{0.0f, 2.0f, 0.0f};
  Vec3 eye{target.x + p.camDist * std::cos(pitch) * std::cos(yaw),
           target.y + p.camDist * std::sin(pitch),
           target.z + p.camDist * std::cos(pitch) * std::sin(yaw)};

  FrameUBO ubo{};
  ubo.view = Mat4::lookAt(eye, target, {0, 1, 0});
  ubo.proj = Mat4::perspective(p.camFovDeg * 3.14159265f / 180.0f,
                               float(p.width) / float(p.height),
                               p.camNear, p.camFar);
  ubo.proj.at(1, 1) *= -1.0f; // Vulkan NDC y-down
  for (int i = 0; i < 3; ++i) ubo.lightVP[i] = lightVP[i];
  ubo.splits = {splits[0], splits[1], splits[2], 0.0f};
  Vec3 L = p.lightDir.normalized();
  ubo.lightDir = {L.x, L.y, L.z, 0.0f};
  ubo.camPos = {eye.x, eye.y, eye.z, 1.0f};

  void* mapped = nullptr;
  vkMapMemory(ctx_->device, ubo_.mem, 0, sizeof(FrameUBO), 0, &mapped);
  std::memcpy(mapped, &ubo, sizeof(FrameUBO));
  vkUnmapMemory(ctx_->device, ubo_.mem);

  vkResetCommandBuffer(cmd_, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd_, &bi);

  Mat4 identity = Mat4::identity();
  VkDeviceSize zero = 0;

  auto recordShadows = [&]() {
    for (int oi = 0; oi < 3; ++oi) {
      int c = job.cascadeOrder[oi];
      VkClearValue clear{};
      clear.depthStencil = {1.0f, 0};
      VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      rbi.renderPass = shadowPass_;
      rbi.framebuffer = shadowFb_[c];
      rbi.renderArea.extent = {shadowSize_, shadowSize_};
      rbi.clearValueCount = 1;
      rbi.pClearValues = &clear;
      vkCmdBeginRenderPass(cmd_, &rbi, VK_SUBPASS_CONTENTS_INLINE);
      VkViewport vp{0, 0, float(shadowSize_), float(shadowSize_), 0.0f, 1.0f};
      VkRect2D sc{{0, 0}, {shadowSize_, shadowSize_}};
      vkCmdSetViewport(cmd_, 0, 1, &vp);
      vkCmdSetScissor(cmd_, 0, 1, &sc);
      vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipe_);
      vkCmdBindVertexBuffers(cmd_, 0, 1, &vbuf_.buf, &zero);
      Mat4 mvp = lightVP[c] * identity;
      vkCmdPushConstants(cmd_, shadowPipeLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                         0, sizeof(Mat4), &mvp);
      vkCmdDraw(cmd_, vertexCount_, 1, 0, 0);
      vkCmdEndRenderPass(cmd_);
    }
  };

  auto recordScene = [&]() {
    VkClearValue clears[2]{};
    clears[0].color = {{0.05f, 0.06f, 0.09f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass = scenePass_;
    rbi.framebuffer = sceneFb_;
    rbi.renderArea.extent = {width_, height_};
    rbi.clearValueCount = 2;
    rbi.pClearValues = clears;
    vkCmdBeginRenderPass(cmd_, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, float(width_), float(height_), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {width_, height_}};
    vkCmdSetViewport(cmd_, 0, 1, &vp);
    vkCmdSetScissor(cmd_, 0, 1, &sc);
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipe_);
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            scenePipeLayout_, 0, 1, &descSet_, 0, nullptr);
    vkCmdBindVertexBuffers(cmd_, 0, 1, &vbuf_.buf, &zero);
    vkCmdPushConstants(cmd_, scenePipeLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(Mat4), &identity);
    vkCmdDraw(cmd_, vertexCount_, 1, 0, 0);
    vkCmdEndRenderPass(cmd_);
  };

  if (job.sceneBeforeShadows) {
    recordScene();
    recordShadows();
  } else {
    recordShadows();
    recordScene();
  }

  if (vkEndCommandBuffer(cmd_) != VK_SUCCESS) {
    err = "vkEndCommandBuffer failed";
    return false;
  }
  return true;
}

bool CsmRenderer::submitAndReadback(std::vector<uint8_t>& outRgba,
                                    std::string& err) {
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd_;
  if (vkQueueSubmit(ctx_->queue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) {
    err = "vkQueueSubmit failed";
    return false;
  }
  vkQueueWaitIdle(ctx_->queue);
  if (!readbackImageRgba(*ctx_, colorTarget_, outRgba)) {
    err = "readback failed";
    return false;
  }
  return true;
}

void CsmRenderer::destroy() {
  if (!ctx_ || !ctx_->device) return;
  VkDevice dev = ctx_->device;
  vkDeviceWaitIdle(dev);
  if (cmd_) vkFreeCommandBuffers(dev, ctx_->cmdPool, 1, &cmd_);
  if (shadowSampler_) vkDestroySampler(dev, shadowSampler_, nullptr);
  if (descPool_) vkDestroyDescriptorPool(dev, descPool_, nullptr);
  if (descLayout_) vkDestroyDescriptorSetLayout(dev, descLayout_, nullptr);
  if (shadowPipe_) vkDestroyPipeline(dev, shadowPipe_, nullptr);
  if (scenePipe_) vkDestroyPipeline(dev, scenePipe_, nullptr);
  if (shadowPipeLayout_) vkDestroyPipelineLayout(dev, shadowPipeLayout_, nullptr);
  if (scenePipeLayout_) vkDestroyPipelineLayout(dev, scenePipeLayout_, nullptr);
  for (int i = 0; i < 3; ++i)
    if (shadowFb_[i]) vkDestroyFramebuffer(dev, shadowFb_[i], nullptr);
  if (sceneFb_) vkDestroyFramebuffer(dev, sceneFb_, nullptr);
  if (shadowPass_) vkDestroyRenderPass(dev, shadowPass_, nullptr);
  if (scenePass_) vkDestroyRenderPass(dev, scenePass_, nullptr);
  for (int i = 0; i < 3; ++i) destroyImage(*ctx_, shadowMaps_[i]);
  destroyImage(*ctx_, colorTarget_);
  destroyImage(*ctx_, depthTarget_);
  destroyBuffer(*ctx_, ubo_);
  destroyBuffer(*ctx_, vbuf_);
  *this = CsmRenderer{};
}
