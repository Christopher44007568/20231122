#include "renderer.hpp"

#include <array>
#include <cstring>

namespace {

bool checkVk(VkResult result, const char* operation, std::string& error) {
  if (result == VK_SUCCESS) {
    return true;
  }
  error = std::string(operation) + " failed with VkResult " +
          std::to_string(static_cast<int>(result));
  return false;
}

}  // namespace

bool Renderer::createOffscreenResources(std::string& error) {
  const std::uint32_t width = static_cast<std::uint32_t>(width_);
  const std::uint32_t height = static_cast<std::uint32_t>(height_);
  const VkImageUsageFlags colorUsage =
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  if (!createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, colorUsage,
                   baselineTarget_.color, error) ||
      !createImage(width, height, VK_FORMAT_R32G32_SFLOAT, colorUsage,
                   baselineTarget_.aux, error) ||
      !createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, colorUsage,
                   currentTarget_.color, error) ||
      !createImage(width, height, VK_FORMAT_R32G32_SFLOAT, colorUsage,
                   currentTarget_.aux, error) ||
      !createImage(width, height, VK_FORMAT_D32_SFLOAT,
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                   depthResource_, error, VK_IMAGE_ASPECT_DEPTH_BIT)) {
    return false;
  }

  const std::array<VkImageView, 3> baselineViews = {
    baselineTarget_.color.view, baselineTarget_.aux.view, depthResource_.view,
  };
  const std::array<VkImageView, 3> currentViews = {
    currentTarget_.color.view, currentTarget_.aux.view, depthResource_.view,
  };
  VkFramebufferCreateInfo framebufferInfo{
    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebufferInfo.renderPass = sceneRenderPass_;
  framebufferInfo.attachmentCount = static_cast<std::uint32_t>(baselineViews.size());
  framebufferInfo.width = width;
  framebufferInfo.height = height;
  framebufferInfo.layers = 1;
  framebufferInfo.pAttachments = baselineViews.data();
  if (!checkVk(vkCreateFramebuffer(device_, &framebufferInfo, nullptr,
                                   &baselineTarget_.framebuffer),
               "vkCreateFramebuffer(baseline)", error)) {
    return false;
  }
  framebufferInfo.pAttachments = currentViews.data();
  return checkVk(vkCreateFramebuffer(device_, &framebufferInfo, nullptr,
                                     &currentTarget_.framebuffer),
                 "vkCreateFramebuffer(current)", error);
}

bool Renderer::createSceneRenderPass(std::string& error) {
  VkAttachmentDescription attachments[3]{};
  attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  attachments[1] = attachments[0];
  attachments[1].format = VK_FORMAT_R32G32_SFLOAT;
  attachments[2].format = VK_FORMAT_D32_SFLOAT;
  attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorReferences[2] = {
    {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
  };
  VkAttachmentReference depthReference{
    2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 2;
  subpass.pColorAttachments = colorReferences;
  subpass.pDepthStencilAttachment = &depthReference;

  const VkSubpassDependency dependencies[2] = {
    {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
     0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0},
    {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0},
  };
  VkRenderPassCreateInfo createInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  createInfo.attachmentCount = 3;
  createInfo.pAttachments = attachments;
  createInfo.subpassCount = 1;
  createInfo.pSubpasses = &subpass;
  createInfo.dependencyCount = 2;
  createInfo.pDependencies = dependencies;
  return checkVk(vkCreateRenderPass(device_, &createInfo, nullptr,
                                    &sceneRenderPass_),
                 "vkCreateRenderPass(scene)", error);
}

bool Renderer::createScenePipeline(std::string& error) {
  VkShaderModule vertexShader = loadShader("scene.vert.spv", error);
  VkShaderModule fragmentShader = loadShader("scene.frag.spv", error);
  if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE) {
    return false;
  }
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertexShader;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragmentShader;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(float) * 4;
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attributes[2]{};
  attributes[0].location = 0;
  attributes[0].binding = 0;
  attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributes[0].offset = 0;
  attributes[1].location = 1;
  attributes[1].binding = 0;
  attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributes[1].offset = sizeof(float) * 2;
  VkPipelineVertexInputStateCreateInfo vertexInput{
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &binding;
  vertexInput.vertexAttributeDescriptionCount = 2;
  vertexInput.pVertexAttributeDescriptions = attributes;

  VkPipelineInputAssemblyStateCreateInfo assembly{
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo viewport{
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster{
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.depthBiasEnable = VK_FALSE;
  raster.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo multisample{
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo depth{
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth.depthTestEnable = VK_TRUE;
  depth.depthWriteEnable = VK_TRUE;
  depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkPipelineColorBlendAttachmentState blendAttachments[2]{};
  blendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
    VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
    VK_COLOR_COMPONENT_A_BIT;
  blendAttachments[1] = blendAttachments[0];
  VkPipelineColorBlendStateCreateInfo blend{
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 2;
  blend.pAttachments = blendAttachments;
  const VkDynamicState dynamicStates[] = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
  };
  VkPipelineDynamicStateCreateInfo dynamic{
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamicStates;

  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                         VK_SHADER_STAGE_FRAGMENT_BIT;
  pushRange.offset = 0;
  pushRange.size = sizeof(float) * 28;
  VkPipelineLayoutCreateInfo layoutInfo{
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pushRange;
  if (!checkVk(vkCreatePipelineLayout(device_, &layoutInfo, nullptr,
                                     &scenePipelineLayout_),
               "vkCreatePipelineLayout(scene)", error)) {
    vkDestroyShaderModule(device_, vertexShader, nullptr);
    vkDestroyShaderModule(device_, fragmentShader, nullptr);
    return false;
  }

  VkGraphicsPipelineCreateInfo pipelineInfo{
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = stages;
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &assembly;
  pipelineInfo.pViewportState = &viewport;
  pipelineInfo.pRasterizationState = &raster;
  pipelineInfo.pMultisampleState = &multisample;
  pipelineInfo.pDepthStencilState = &depth;
  pipelineInfo.pColorBlendState = &blend;
  pipelineInfo.pDynamicState = &dynamic;
  pipelineInfo.layout = scenePipelineLayout_;
  pipelineInfo.renderPass = sceneRenderPass_;
  pipelineInfo.subpass = 0;

  raster.depthClampEnable = VK_FALSE;
  const VkResult baselineResult = vkCreateGraphicsPipelines(
    device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sceneBaselinePipeline_);
  raster.depthClampEnable = VK_TRUE;
  const VkResult scenarioResult = vkCreateGraphicsPipelines(
    device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sceneScenarioPipeline_);
  vkDestroyShaderModule(device_, vertexShader, nullptr);
  vkDestroyShaderModule(device_, fragmentShader, nullptr);
  if (baselineResult != VK_SUCCESS || scenarioResult != VK_SUCCESS) {
    error = "vkCreateGraphicsPipelines(scene) failed";
    return false;
  }
  return true;
}

bool Renderer::createQuadBuffer(std::string& error) {
  const float vertices[] = {
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 0.0f,
  };
  if (!createBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    quadBuffer_, quadMemory_, error)) {
    return false;
  }
  void* mapped = nullptr;
  if (!checkVk(vkMapMemory(device_, quadMemory_, 0, sizeof(vertices), 0, &mapped),
               "vkMapMemory(quad)", error)) {
    return false;
  }
  std::memcpy(mapped, vertices, sizeof(vertices));
  vkUnmapMemory(device_, quadMemory_);
  return true;
}

bool Renderer::createStagingBuffer(std::string& error) {
  const VkDeviceSize colorBytes =
    static_cast<VkDeviceSize>(width_) * height_ * 4;
  const VkDeviceSize auxBytes =
    static_cast<VkDeviceSize>(width_) * height_ * sizeof(PixelAux);
  stagingSize_ = colorBytes + auxBytes;
  return createBuffer(stagingSize_, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      stagingBuffer_, stagingMemory_, error);
}

bool Renderer::createUiBuffer(std::string& error) {
  uiBufferSize_ = 4u * 1024u * 1024u;
  return createBuffer(uiBufferSize_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      uiBuffer_, uiMemory_, error);
}

std::uint32_t Renderer::pickObject(int x, int y) const {
  return samplePixel(baselineFrame_, x, y).objectId;
}



