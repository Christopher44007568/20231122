#pragma once

#include "capture_format.h"
#include "image.h"
#include "vk_context.h"

#include <map>
#include <vector>

// Offscreen renderer used by both the recorder and the replayer.
// Pipelines are cached per primitive topology so a replayed frame can mix
// topologies exactly as the capture dictates.
class Renderer {
public:
  Renderer(VkContext& ctx, uint32_t width, uint32_t height);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  void uploadVertices(const std::vector<ptdc::Vertex>& verts);

  // Renders all draws and returns the final framebuffer contents.
  Image renderFrame(const std::vector<ptdc::DrawCall>& draws);

  // Renders all draws, snapshotting the framebuffer after each draw call.
  // Returned vector has one image per draw call (checkpoint i = state after
  // draw i). This is what correlates image differences to draw calls.
  std::vector<Image> renderWithCheckpoints(const std::vector<ptdc::DrawCall>& draws);

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }

private:
  VkPipeline pipelineFor(uint32_t topology);
  void recordDraws(VkCommandBuffer cmd, const std::vector<ptdc::DrawCall>& draws,
                   const std::vector<VkBuffer>* checkpointBuffers);
  Image readBack();

  VkContext& ctx_;
  uint32_t width_;
  uint32_t height_;

  VkImage colorImage_ = VK_NULL_HANDLE;
  VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
  VkImageView colorView_ = VK_NULL_HANDLE;
  VkRenderPass renderPassClear_ = VK_NULL_HANDLE;
  VkRenderPass renderPassLoad_ = VK_NULL_HANDLE;
  VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
  VkShaderModule vertModule_ = VK_NULL_HANDLE;
  VkShaderModule fragModule_ = VK_NULL_HANDLE;
  std::map<uint32_t, VkPipeline> pipelines_;

  VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
  VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
  VkDeviceSize vertexBufferSize_ = 0;
};
