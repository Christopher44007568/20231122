#include "renderer.hpp"

#include "scene.hpp"

#include <algorithm>
#include <chrono>
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

struct ScenePush {
  float viewProjection[16];
  float baseColor[4];
  float geometry[4];
  float identity[4];
};

static_assert(sizeof(ScenePush) == 112, "scene push constant size");

}  // namespace

bool Renderer::renderTarget(const SceneTarget& target, const DepthParams& params,
                            bool baselinePass, std::uint32_t focusId,
                            const BiasCandidate& focusBias,
                            std::string& error) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();
  if (commandBuffer == VK_NULL_HANDLE) {
    error = "Cannot allocate scene command buffer";
    return false;
  }
  VkClearValue clears[3]{};
  clears[0].color = {{0.030f, 0.043f, 0.058f, 1.0f}};
  clears[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
  clears[2].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo beginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  beginInfo.renderPass = sceneRenderPass_;
  beginInfo.framebuffer = target.framebuffer;
  beginInfo.renderArea = {{0, 0}, {static_cast<std::uint32_t>(width_),
                                   static_cast<std::uint32_t>(height_)}};
  beginInfo.clearValueCount = 3;
  beginInfo.pClearValues = clears;
  vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

  const VkPipeline pipeline =
    baselinePass ? sceneBaselinePipeline_
                 : (params.depthClamp ? sceneScenarioPipeline_
                                      : sceneBaselinePipeline_);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  VkViewport viewport{0.0f, 0.0f, static_cast<float>(width_),
                      static_cast<float>(height_), params.viewportMinDepth,
                      params.viewportMaxDepth};
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  VkRect2D scissor{{0, 0},
    {static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_)}};
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

  const VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &quadBuffer_, &offset);
  const Scene scene = Scene::makeDefault();
  const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
  const Mat4 projection =
    perspectiveVulkan(55.0f, aspect, params.nearZ, params.farZ);
  for (const RenderObject& object : scene.objects()) {
    ScenePush push{};
    std::memcpy(push.viewProjection, projection.m, sizeof(projection.m));
    push.baseColor[0] = object.color.x;
    push.baseColor[1] = object.color.y;
    push.baseColor[2] = object.color.z;
    push.baseColor[3] = 1.0f;
    push.geometry[0] = object.center.x;
    push.geometry[1] = object.center.y;
    push.geometry[2] = object.halfSize.x;
    push.geometry[3] = object.halfSize.y;
    push.identity[0] = object.center.z;
    push.identity[1] = static_cast<float>(object.id);
    push.identity[2] = object.borderPhase;
    if (!baselinePass && object.id == focusId) {
      push.identity[3] = focusBias.constant + focusBias.slope * 16.0f;
    }
    vkCmdPushConstants(commandBuffer, scenePipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(commandBuffer, 6, 1, 0, 0);
  }
  vkCmdEndRenderPass(commandBuffer);
  return endSingleTimeCommands(commandBuffer, error);
}

bool Renderer::readbackTarget(const SceneTarget& target, FrameData& frame,
                              std::string& error) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();
  if (commandBuffer == VK_NULL_HANDLE) {
    error = "Cannot allocate readback command buffer";
    return false;
  }
  VkImageMemoryBarrier toTransfer[2]{};
  VkImage images[2] = {target.color.image, target.aux.image};
  for (int index = 0; index < 2; ++index) {
    toTransfer[index].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer[index].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toTransfer[index].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toTransfer[index].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toTransfer[index].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer[index].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer[index].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer[index].image = images[index];
    toTransfer[index].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer[index].subresourceRange.levelCount = 1;
    toTransfer[index].subresourceRange.layerCount = 1;
  }
  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 2, toTransfer);

  const VkDeviceSize colorBytes =
    static_cast<VkDeviceSize>(width_) * height_ * 4;
  VkBufferImageCopy colorCopy{};
  colorCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  colorCopy.imageSubresource.layerCount = 1;
  colorCopy.imageExtent = {static_cast<std::uint32_t>(width_),
                           static_cast<std::uint32_t>(height_), 1};
  vkCmdCopyImageToBuffer(commandBuffer, target.color.image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         stagingBuffer_, 1, &colorCopy);
  VkBufferImageCopy auxCopy = colorCopy;
  auxCopy.bufferOffset = colorBytes;
  vkCmdCopyImageToBuffer(commandBuffer, target.aux.image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         stagingBuffer_, 1, &auxCopy);

  VkImageMemoryBarrier toShader[2] = {};
  for (int index = 0; index < 2; ++index) {
    toShader[index] = toTransfer[index];
    toShader[index].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toShader[index].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShader[index].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toShader[index].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }
  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 2, toShader);
  if (!endSingleTimeCommands(commandBuffer, error)) {
    return false;
  }

  void* mapped = nullptr;
  if (!checkVk(vkMapMemory(device_, stagingMemory_, 0, stagingSize_, 0, &mapped),
               "vkMapMemory(readback)", error)) {
    return false;
  }
  frame.width = width_;
  frame.height = height_;
  frame.rgba.resize(static_cast<std::size_t>(colorBytes));
  frame.aux.resize(static_cast<std::size_t>(width_) * height_);
  std::memcpy(frame.rgba.data(), mapped, static_cast<std::size_t>(colorBytes));
  std::memcpy(frame.aux.data(),
              static_cast<const std::uint8_t*>(mapped) + colorBytes,
              static_cast<std::size_t>(width_) * height_ * sizeof(PixelAux));
  vkUnmapMemory(device_, stagingMemory_);
  return true;
}

bool Renderer::renderBaseline(const DepthParams& params, std::string& error) {
  if (headless_) {
    vkDeviceWaitIdle(device_);
  }
  if (!renderTarget(baselineTarget_, params, true, 0, {}, error)) {
    return false;
  }
  return readbackTarget(baselineTarget_, baselineFrame_, error);
}

bool Renderer::renderScenario(const DepthParams& params, std::uint32_t focusId,
                              const BiasCandidate& focusBias, FrameData& output,
                              double& elapsedMs, std::string& error) {
  const auto start = std::chrono::steady_clock::now();
  if (!renderTarget(currentTarget_, params, false, focusId, focusBias, error) ||
      !readbackTarget(currentTarget_, currentFrame_, error)) {
    return false;
  }
  diffFrame_ = buildDiffFrame(baselineFrame_, currentFrame_, focusId);
  const auto end = std::chrono::steady_clock::now();
  elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
  output = currentFrame_;
  return true;
}

bool Renderer::uploadUi(const UiFrame& ui, std::string& error) {
  const VkDeviceSize bytes =
    static_cast<VkDeviceSize>(ui.vertices.size()) * sizeof(UiVertex);
  if (bytes > uiBufferSize_) {
    error = "UI vertex buffer overflow";
    return false;
  }
  if (bytes == 0) {
    return true;
  }
  void* mapped = nullptr;
  if (!checkVk(vkMapMemory(device_, uiMemory_, 0, bytes, 0, &mapped),
               "vkMapMemory(ui)", error)) {
    return false;
  }
  std::memcpy(mapped, ui.vertices.data(), static_cast<std::size_t>(bytes));
  vkUnmapMemory(device_, uiMemory_);
  return true;
}

bool Renderer::present(int mode, std::uint32_t focusId, float split,
                       const UiFrame& ui, std::string& error) {
  if (headless_) {
    return true;
  }
  if (swapchain_ == VK_NULL_HANDLE || swapchainFramebuffers_.empty()) {
    error = "Swapchain is not initialized";
    return false;
  }
  if (!uploadUi(ui, error)) {
    return false;
  }
  std::uint32_t imageIndex = 0;
  const VkResult acquire = vkAcquireNextImageKHR(
    device_, swapchain_, UINT64_MAX, imageAvailable_, VK_NULL_HANDLE, &imageIndex);
  if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
    return true;
  }
  if (!checkVk(acquire, "vkAcquireNextImageKHR", error)) {
    return false;
  }
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();
  if (commandBuffer == VK_NULL_HANDLE) {
    error = "Cannot allocate present command buffer";
    return false;
  }
  VkClearValue clear{};
  clear.color = {{0.035f, 0.051f, 0.070f, 1.0f}};
  VkRenderPassBeginInfo beginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  beginInfo.renderPass = swapRenderPass_;
  beginInfo.framebuffer = swapchainFramebuffers_[imageIndex];
  beginInfo.renderArea = {{0, 0}, swapchainExtent_};
  beginInfo.clearValueCount = 1;
  beginInfo.pClearValues = &clear;
  vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    compositePipeline_);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          compositePipelineLayout_, 0, 1, &compositeSet_, 0,
                          nullptr);
  const float controls[4] = {
    static_cast<float>(std::max(0, std::min(3, mode))),
    static_cast<float>(focusId),
    split,
    0.0f,
  };
  vkCmdPushConstants(commandBuffer, compositePipelineLayout_,
                     VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(controls),
                     controls);
  vkCmdDraw(commandBuffer, 3, 1, 0, 0);

  if (!ui.vertices.empty()) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      uiPipeline_);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &uiBuffer_, &offset);
    const float screenSize[2] = {
      static_cast<float>(swapchainExtent_.width),
      static_cast<float>(swapchainExtent_.height),
    };
    vkCmdPushConstants(commandBuffer, uiPipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(screenSize),
                       screenSize);
    vkCmdDraw(commandBuffer, static_cast<std::uint32_t>(ui.vertices.size()),
              1, 0, 0);
  }
  vkCmdEndRenderPass(commandBuffer);
  if (!checkVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(present)",
               error)) {
    return false;
  }
  const VkPipelineStageFlags waitStage =
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &imageAvailable_;
  submitInfo.pWaitDstStageMask = &waitStage;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  if (!checkVk(vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE),
               "vkQueueSubmit(present)", error) ||
      !checkVk(vkQueueWaitIdle(queue_), "vkQueueWaitIdle(present)", error)) {
    return false;
  }
  VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &swapchain_;
  presentInfo.pImageIndices = &imageIndex;
  const VkResult presentResult = vkQueuePresentKHR(queue_, &presentInfo);
  vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
  if (presentResult != VK_SUCCESS &&
      presentResult != VK_ERROR_OUT_OF_DATE_KHR &&
      presentResult != VK_SUBOPTIMAL_KHR) {
    return checkVk(presentResult, "vkQueuePresentKHR", error);
  }
  return true;
}


