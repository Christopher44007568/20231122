#include "renderer.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstddef>

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

bool Renderer::createSwapchain(std::string& error) {
  if (headless_) {
    return true;
  }
  VkSurfaceCapabilitiesKHR capabilities{};
  if (!checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                 physicalDevice_, surface_, &capabilities),
               "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", error)) {
    return false;
  }

  std::uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount,
                                       nullptr);
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount,
                                       formats.data());
  VkSurfaceFormatKHR selectedFormat = formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
  for (const VkSurfaceFormatKHR& format : formats) {
    if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
        (format.format == VK_FORMAT_B8G8R8A8_UNORM ||
         format.format == VK_FORMAT_R8G8B8A8_UNORM)) {
      selectedFormat = format;
      break;
    }
  }
  swapchainFormat_ = selectedFormat.format;

  std::uint32_t modeCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount,
                                            nullptr);
  std::vector<VkPresentModeKHR> modes(modeCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount,
                                            modes.data());
  VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
  for (VkPresentModeKHR mode : modes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      presentMode = mode;
    }
  }

  if (capabilities.currentExtent.width != UINT32_MAX) {
    swapchainExtent_ = capabilities.currentExtent;
  } else {
    swapchainExtent_.width = static_cast<std::uint32_t>(std::max(
      1, std::min(width_, static_cast<int>(capabilities.maxImageExtent.width))));
    swapchainExtent_.height = static_cast<std::uint32_t>(std::max(
      1, std::min(height_, static_cast<int>(capabilities.maxImageExtent.height))));
  }
  std::uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  createInfo.surface = surface_;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = selectedFormat.format;
  createInfo.imageColorSpace = selectedFormat.colorSpace;
  createInfo.imageExtent = swapchainExtent_;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  createInfo.preTransform = capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;
  if (!checkVk(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_),
               "vkCreateSwapchainKHR", error)) {
    return false;
  }
  std::uint32_t actualCount = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &actualCount, nullptr);
  swapchainImages_.resize(actualCount);
  vkGetSwapchainImagesKHR(device_, swapchain_, &actualCount,
                          swapchainImages_.data());
  swapchainViews_.resize(actualCount, VK_NULL_HANDLE);
  for (std::uint32_t index = 0; index < actualCount; ++index) {
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = swapchainImages_[index];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = swapchainFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (!checkVk(vkCreateImageView(device_, &viewInfo, nullptr,
                                   &swapchainViews_[index]),
                 "vkCreateImageView(swapchain)", error)) {
      return false;
    }
  }
  return true;
}

bool Renderer::createSwapRenderPass(std::string& error) {
  VkAttachmentDescription attachment{};
  attachment.format = swapchainFormat_;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &reference;
  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo createInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  createInfo.attachmentCount = 1;
  createInfo.pAttachments = &attachment;
  createInfo.subpassCount = 1;
  createInfo.pSubpasses = &subpass;
  createInfo.dependencyCount = 1;
  createInfo.pDependencies = &dependency;
  return checkVk(vkCreateRenderPass(device_, &createInfo, nullptr,
                                    &swapRenderPass_),
                 "vkCreateRenderPass(swapchain)", error);
}

bool Renderer::createCompositeResources(std::string& error) {
  VkSamplerCreateInfo colorSamplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  colorSamplerInfo.magFilter = VK_FILTER_LINEAR;
  colorSamplerInfo.minFilter = VK_FILTER_LINEAR;
  colorSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  colorSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  colorSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  colorSamplerInfo.maxLod = 0.0f;
  if (!checkVk(vkCreateSampler(device_, &colorSamplerInfo, nullptr,
                               &colorSampler_),
               "vkCreateSampler(color)", error)) {
    return false;
  }
  VkSamplerCreateInfo auxSamplerInfo = colorSamplerInfo;
  auxSamplerInfo.magFilter = VK_FILTER_NEAREST;
  auxSamplerInfo.minFilter = VK_FILTER_NEAREST;
  if (!checkVk(vkCreateSampler(device_, &auxSamplerInfo, nullptr,
                               &auxSampler_),
               "vkCreateSampler(aux)", error)) {
    return false;
  }

  VkDescriptorSetLayoutBinding bindings[4]{};
  for (std::uint32_t index = 0; index < 4; ++index) {
    bindings[index].binding = index;
    bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[index].descriptorCount = 1;
    bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  VkDescriptorSetLayoutCreateInfo layoutInfo{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = 4;
  layoutInfo.pBindings = bindings;
  if (!checkVk(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr,
                                           &compositeSetLayout_),
               "vkCreateDescriptorSetLayout(composite)", error)) {
    return false;
  }
  VkDescriptorPoolSize poolSize{};
  poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSize.descriptorCount = 4;
  VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.maxSets = 1;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  if (!checkVk(vkCreateDescriptorPool(device_, &poolInfo, nullptr,
                                      &descriptorPool_),
               "vkCreateDescriptorPool", error)) {
    return false;
  }
  VkDescriptorSetAllocateInfo allocateInfo{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocateInfo.descriptorPool = descriptorPool_;
  allocateInfo.descriptorSetCount = 1;
  allocateInfo.pSetLayouts = &compositeSetLayout_;
  if (!checkVk(vkAllocateDescriptorSets(device_, &allocateInfo, &compositeSet_),
               "vkAllocateDescriptorSets", error)) {
    return false;
  }
  VkDescriptorImageInfo images[4]{};
  images[0] = {colorSampler_, baselineTarget_.color.view,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  images[1] = {auxSampler_, baselineTarget_.aux.view,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  images[2] = {colorSampler_, currentTarget_.color.view,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  images[3] = {auxSampler_, currentTarget_.aux.view,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet writes[4]{};
  for (std::uint32_t index = 0; index < 4; ++index) {
    writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[index].dstSet = compositeSet_;
    writes[index].dstBinding = index;
    writes[index].descriptorCount = 1;
    writes[index].descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[index].pImageInfo = &images[index];
  }
  vkUpdateDescriptorSets(device_, 4, writes, 0, nullptr);

  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  pushRange.size = sizeof(float) * 4;
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &compositeSetLayout_;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;
  if (!checkVk(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr,
                                      &compositePipelineLayout_),
               "vkCreatePipelineLayout(composite)", error)) {
    return false;
  }

  VkShaderModule vertexShader = loadShader("composite.vert.spv", error);
  VkShaderModule fragmentShader = loadShader("composite.frag.spv", error);
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
  VkPipelineVertexInputStateCreateInfo vertexInput{
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo assembly{
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport viewport{0.0f, 0.0f, static_cast<float>(swapchainExtent_.width),
                      static_cast<float>(swapchainExtent_.height), 0.0f, 1.0f};
  VkRect2D scissor{{0, 0}, swapchainExtent_};
  VkPipelineViewportStateCreateInfo viewportState{
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;
  VkPipelineRasterizationStateCreateInfo raster{
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo multisample{
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState blendAttachment{};
  blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
    VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
    VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blendAttachment;
  VkGraphicsPipelineCreateInfo pipelineInfo{
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = stages;
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &assembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &raster;
  pipelineInfo.pMultisampleState = &multisample;
  pipelineInfo.pColorBlendState = &blend;
  pipelineInfo.layout = compositePipelineLayout_;
  pipelineInfo.renderPass = swapRenderPass_;
  pipelineInfo.subpass = 0;
  const VkResult result = vkCreateGraphicsPipelines(
    device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compositePipeline_);
  vkDestroyShaderModule(device_, vertexShader, nullptr);
  vkDestroyShaderModule(device_, fragmentShader, nullptr);
  if (result != VK_SUCCESS) {
    error = "vkCreateGraphicsPipelines(composite) failed";
    return false;
  }

  swapchainFramebuffers_.resize(swapchainViews_.size(), VK_NULL_HANDLE);
  for (std::size_t index = 0; index < swapchainViews_.size(); ++index) {
    VkFramebufferCreateInfo framebufferInfo{
      VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferInfo.renderPass = swapRenderPass_;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &swapchainViews_[index];
    framebufferInfo.width = swapchainExtent_.width;
    framebufferInfo.height = swapchainExtent_.height;
    framebufferInfo.layers = 1;
    if (!checkVk(vkCreateFramebuffer(device_, &framebufferInfo, nullptr,
                                     &swapchainFramebuffers_[index]),
                 "vkCreateFramebuffer(swapchain)", error)) {
      return false;
    }
  }
  return true;
}

bool Renderer::createUiPipeline(std::string& error) {
  VkShaderModule vertexShader = loadShader("ui.vert.spv", error);
  VkShaderModule fragmentShader = loadShader("ui.frag.spv", error);
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
  VkVertexInputBindingDescription binding{0, sizeof(UiVertex),
    VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription attributes[2]{};
  attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
  attributes[1] = {1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(UiVertex, r)};
  VkPipelineVertexInputStateCreateInfo vertexInput{
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &binding;
  vertexInput.vertexAttributeDescriptionCount = 2;
  vertexInput.pVertexAttributeDescriptions = attributes;
  VkPipelineInputAssemblyStateCreateInfo assembly{
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport viewport{0.0f, 0.0f, static_cast<float>(swapchainExtent_.width),
                      static_cast<float>(swapchainExtent_.height), 0.0f, 1.0f};
  VkRect2D scissor{{0, 0}, swapchainExtent_};
  VkPipelineViewportStateCreateInfo viewportState{
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;
  VkPipelineRasterizationStateCreateInfo raster{
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo multisample{
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState blendAttachment{};
  blendAttachment.blendEnable = VK_TRUE;
  blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
    VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
    VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blendAttachment;
  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pushRange.size = sizeof(float) * 2;
  VkPipelineLayoutCreateInfo layoutInfo{
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pushRange;
  if (!checkVk(vkCreatePipelineLayout(device_, &layoutInfo, nullptr,
                                      &uiPipelineLayout_),
               "vkCreatePipelineLayout(ui)", error)) {
    return false;
  }
  VkGraphicsPipelineCreateInfo pipelineInfo{
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = stages;
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &assembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &raster;
  pipelineInfo.pMultisampleState = &multisample;
  pipelineInfo.pColorBlendState = &blend;
  pipelineInfo.layout = uiPipelineLayout_;
  pipelineInfo.renderPass = swapRenderPass_;
  pipelineInfo.subpass = 0;
  const VkResult result = vkCreateGraphicsPipelines(
    device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &uiPipeline_);
  vkDestroyShaderModule(device_, vertexShader, nullptr);
  vkDestroyShaderModule(device_, fragmentShader, nullptr);
  if (result != VK_SUCCESS) {
    error = "vkCreateGraphicsPipelines(ui) failed";
    return false;
  }
  return true;
}


