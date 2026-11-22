#include "scene.h"

#include <cstring>
#include <stdexcept>

#include "scene_vert.spv.h"
#include "scene_frag.spv.h"
#include "floor_frag.spv.h"

#define VK_CHECK(x)                                                     \
  do {                                                                  \
    VkResult r_ = (x);                                                  \
    if (r_ != VK_SUCCESS)                                               \
      throw std::runtime_error(std::string("vk error ") +               \
                               std::to_string(r_) + " at " #x);         \
  } while (0)

namespace {
constexpr uint32_t kReflSize = 256;
constexpr uint32_t kMainW = 512, kMainH = 512;

struct Vert { float x, y, r, g, b; };

const Vert kFloor[] = {
    {-1.0f, 0.0f, 0.28f, 0.28f, 0.32f}, {1.0f, 0.0f, 0.32f, 0.32f, 0.36f},
    {1.0f, 1.0f, 0.24f, 0.24f, 0.28f},  {-1.0f, 0.0f, 0.28f, 0.28f, 0.32f},
    {1.0f, 1.0f, 0.24f, 0.24f, 0.28f},  {-1.0f, 1.0f, 0.22f, 0.22f, 0.26f},
};
const Vert kObjA[] = {
    {-0.55f, -0.15f, 0.9f, 0.25f, 0.2f}, {-0.15f, -0.15f, 0.8f, 0.2f, 0.2f},
    {-0.35f, -0.75f, 0.95f, 0.4f, 0.3f},
};
const Vert kObjB[] = {
    {0.2f, -0.1f, 0.2f, 0.85f, 0.3f}, {0.6f, -0.1f, 0.25f, 0.7f, 0.9f},
    {0.4f, -0.7f, 0.3f, 0.9f, 0.5f},
};

VkShaderModule makeModule(VkDevice dev, const uint32_t* code, size_t bytes) {
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = bytes;
  ci.pCode = code;
  VkShaderModule m;
  VK_CHECK(vkCreateShaderModule(dev, &ci, nullptr, &m));
  return m;
}

VkRenderPass makePass(VkDevice dev, VkAttachmentLoadOp load) {
  VkAttachmentDescription att{};
  att.format = VK_FORMAT_R8G8B8A8_UNORM;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = load;
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
  VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  ci.attachmentCount = 1;
  ci.pAttachments = &att;
  ci.subpassCount = 1;
  ci.pSubpasses = &sub;
  VkRenderPass rp;
  VK_CHECK(vkCreateRenderPass(dev, &ci, nullptr, &rp));
  return rp;
}
}  // namespace

std::vector<DrawStep> SceneRenderer::fullFrame() {
  auto mk = [](const char* n, uint32_t pipe, uint32_t mesh, uint32_t fb,
               float r, float g, float b, bool mirror, uint32_t verts) {
    DrawStep s;
    s.name = n; s.pipelineId = pipe; s.meshId = mesh; s.fbTarget = fb;
    s.tint[0] = r; s.tint[1] = g; s.tint[2] = b;
    s.mirror = mirror; s.vertexCount = verts;
    return s;
  };
  return {
      mk("refl/floor",        0, 0, 0, 0.9f, 0.9f, 0.9f, true, 6),
      mk("refl/objectA",      0, 1, 0, 1.0f, 1.0f, 1.0f, true, 3),
      mk("refl/objectB",      0, 2, 0, 1.0f, 1.0f, 1.0f, true, 3),
      mk("main/floor-reflect",1, 0, 1, 1.0f, 1.0f, 1.0f, false, 6),
      mk("main/objectA",      0, 1, 1, 1.0f, 1.0f, 1.0f, false, 3),
      mk("main/objectB",      0, 2, 1, 1.0f, 1.0f, 1.0f, false, 3),
  };
}

uint64_t SceneRenderer::stateHash(const DrawStep& s) {
  struct {
    uint32_t pipe, mesh, fb, mirror, verts;
    float tint[3];
  } h{s.pipelineId, s.meshId, s.fbTarget, s.mirror ? 1u : 0u, s.vertexCount,
      {s.tint[0], s.tint[1], s.tint[2]}};
  return trace::fnv1a64(&h, sizeof(h));
}

void SceneRenderer::init(Gpu& g) {
  g_ = &g;
  VkDevice dev = g.dev;
  VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  refl_ = createImage(g, kReflSize, kReflSize,
                      colorUsage | VK_IMAGE_USAGE_SAMPLED_BIT);
  main_ = createImage(g, kMainW, kMainH, colorUsage);
  transition(g, refl_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  transition(g, main_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  rpClear_ = makePass(dev, VK_ATTACHMENT_LOAD_OP_CLEAR);
  rpLoad_ = makePass(dev, VK_ATTACHMENT_LOAD_OP_LOAD);

  auto makeFb = [&](VkImageView view, uint32_t w, uint32_t h) {
    VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    ci.renderPass = rpClear_;
    ci.attachmentCount = 1;
    ci.pAttachments = &view;
    ci.width = w; ci.height = h; ci.layers = 1;
    VkFramebuffer fb;
    VK_CHECK(vkCreateFramebuffer(dev, &ci, nullptr, &fb));
    return fb;
  };
  fbRefl_ = makeFb(refl_.view, kReflSize, kReflSize);
  fbMain_ = makeFb(main_.view, kMainW, kMainH);

  // Descriptor: reflection texture for the floor pipeline.
  VkDescriptorSetLayoutBinding bind{};
  bind.binding = 0;
  bind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bind.descriptorCount = 1;
  bind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dci.bindingCount = 1;
  dci.pBindings = &bind;
  VK_CHECK(vkCreateDescriptorSetLayout(dev, &dci, nullptr, &dsl_));
  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
  VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pci.maxSets = 1;
  pci.poolSizeCount = 1;
  pci.pPoolSizes = &ps;
  VK_CHECK(vkCreateDescriptorPool(dev, &pci, nullptr, &dp_));
  VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dai.descriptorPool = dp_;
  dai.descriptorSetCount = 1;
  dai.pSetLayouts = &dsl_;
  VK_CHECK(vkAllocateDescriptorSets(dev, &dai, &ds_));

  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = VK_FILTER_LINEAR;
  sci.minFilter = VK_FILTER_LINEAR;
  VK_CHECK(vkCreateSampler(dev, &sci, nullptr, &sampler_));
  VkDescriptorImageInfo ii{sampler_, refl_.view,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  w.dstSet = ds_;
  w.dstBinding = 0;
  w.descriptorCount = 1;
  w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  w.pImageInfo = &ii;
  vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);

  // Pipeline layouts (push constants: mat4 + 2 vec4).
  VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, 96};
  VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges = &pcr;
  VK_CHECK(vkCreatePipelineLayout(dev, &pli, nullptr, &layFlat_));
  pli.setLayoutCount = 1;
  pli.pSetLayouts = &dsl_;
  VK_CHECK(vkCreatePipelineLayout(dev, &pli, nullptr, &layFloor_));

  VkShaderModule vs = makeModule(dev, spv_scene_vert, sizeof(spv_scene_vert));
  VkShaderModule fsFlat = makeModule(dev, spv_scene_frag, sizeof(spv_scene_frag));
  VkShaderModule fsFloor = makeModule(dev, spv_floor_frag, sizeof(spv_floor_frag));

  auto makePipe = [&](VkShaderModule fs, VkPipelineLayout layout) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1] = stages[0];
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    VkVertexInputBindingDescription vb{0, sizeof(Vert), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[2] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 8}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = va;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dyn;
    gp.layout = layout;
    gp.renderPass = rpClear_;
    VkPipeline p;
    VK_CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp, nullptr, &p));
    return p;
  };
  pipeFlat_ = makePipe(fsFlat, layFlat_);
  pipeFloor_ = makePipe(fsFloor, layFloor_);
  vkDestroyShaderModule(dev, vs, nullptr);
  vkDestroyShaderModule(dev, fsFlat, nullptr);
  vkDestroyShaderModule(dev, fsFloor, nullptr);

  const Vert* data[3] = {kFloor, kObjA, kObjB};
  const size_t counts[3] = {6, 3, 3};
  for (int i = 0; i < 3; ++i) {
    meshes_[i] = createBuffer(g, counts[i] * sizeof(Vert),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              true);
    memcpy(meshes_[i].mapped, data[i], counts[i] * sizeof(Vert));
  }
}

void SceneRenderer::destroy() {
  VkDevice dev = g_->dev;
  vkDeviceWaitIdle(dev);
  for (auto& m : meshes_) destroyBuffer(*g_, m);
  vkDestroyPipeline(dev, pipeFlat_, nullptr);
  vkDestroyPipeline(dev, pipeFloor_, nullptr);
  vkDestroyPipelineLayout(dev, layFlat_, nullptr);
  vkDestroyPipelineLayout(dev, layFloor_, nullptr);
  vkDestroySampler(dev, sampler_, nullptr);
  vkDestroyDescriptorPool(dev, dp_, nullptr);
  vkDestroyDescriptorSetLayout(dev, dsl_, nullptr);
  vkDestroyFramebuffer(dev, fbRefl_, nullptr);
  vkDestroyFramebuffer(dev, fbMain_, nullptr);
  vkDestroyRenderPass(dev, rpClear_, nullptr);
  vkDestroyRenderPass(dev, rpLoad_, nullptr);
  destroyImage(*g_, refl_);
  destroyImage(*g_, main_);
}

void SceneRenderer::drawOne(const DrawStep& s) {
  Gpu& g = *g_;
  GpuImage& target = s.fbTarget == 0 ? refl_ : main_;
  VkFramebuffer fb = s.fbTarget == 0 ? fbRefl_ : fbMain_;
  VkCommandBuffer cmd = g.beginCmd();
  VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rbi.renderPass = cleared_[s.fbTarget] ? rpLoad_ : rpClear_;
  rbi.framebuffer = fb;
  rbi.renderArea.extent = {target.w, target.h};
  VkClearValue clear{};
  clear.color = {{0.05f, 0.06f, 0.09f, 1.0f}};
  rbi.clearValueCount = 1;
  rbi.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vp{0, 0, float(target.w), float(target.h), 0, 1};
  VkRect2D sc{{0, 0}, {target.w, target.h}};
  vkCmdSetViewport(cmd, 0, 1, &vp);
  vkCmdSetScissor(cmd, 0, 1, &sc);
  bool floor = s.pipelineId == 1;
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    floor ? pipeFloor_ : pipeFlat_);
  if (floor)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layFloor_,
                            0, 1, &ds_, 0, nullptr);
  VkDeviceSize off = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &meshes_[s.meshId].buf, &off);
  float pc[24] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
                  s.tint[0], s.tint[1], s.tint[2], 1.0f,
                  s.mirror ? -1.0f : 1.0f, 0, 0, 0};
  vkCmdPushConstants(cmd, floor ? layFloor_ : layFlat_,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), pc);
  vkCmdDraw(cmd, s.vertexCount, 1, 0, 0);
  vkCmdEndRenderPass(cmd);
  g.submitWait(cmd);
  cleared_[s.fbTarget] = true;
}

Image SceneRenderer::run(const std::vector<DrawStep>& steps, trace::Trace* rec,
                         std::vector<uint64_t>* fbHashes) {
  Gpu& g = *g_;
  uint32_t idx = 0;
  bool reflAsTexture = false;
  auto emitRes = [&](const char* name, uint32_t resId, VkImageLayout from,
                     VkImageLayout to) {
    if (!rec) return;
    trace::Record r;
    r.type = trace::RecordType::kResourceState;
    r.res.index = idx++;
    r.res.name = name;
    r.res.resourceId = resId;
    r.res.oldState = uint32_t(from);
    r.res.newState = uint32_t(to);
    rec->records.push_back(std::move(r));
  };
  // Semantic records for the initial layout transitions done in init().
  emitRes("reflectionTex", 1, VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  emitRes("mainColor", 2, VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  for (const DrawStep& s : steps) {
    if (s.pipelineId == 1 && !reflAsTexture) {
      transition(g, refl_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      reflAsTexture = true;
      emitRes("reflectionTex", 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    drawOne(s);
    GpuImage& target = s.fbTarget == 0 ? refl_ : main_;
    Image snap = readback(g, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    uint64_t h = trace::fnv1a64(snap.rgba.data(), snap.rgba.size());
    if (fbHashes) fbHashes->push_back(h);
    if (rec) {
      trace::Record r;
      r.type = trace::RecordType::kDraw;
      r.draw.index = idx++;
      r.draw.name = s.name;
      r.draw.pipelineId = s.pipelineId;
      r.draw.vertexCount = s.vertexCount;
      r.draw.fbTarget = s.fbTarget;
      r.draw.stateHash = stateHash(s);
      r.draw.fbHashAfter = h;
      rec->records.push_back(std::move(r));
    }
  }
  emitRes("mainColor", 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  Image final = readback(g, main_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  if (rec)
    rec->finalFbHash = trace::fnv1a64(final.rgba.data(), final.rgba.size());
  return final;
}

Image SceneRenderer::renderReference() {
  return run(fullFrame(), nullptr, nullptr);
}
