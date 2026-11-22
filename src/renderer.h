#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

#include "paging.h"
#include "png.h"
#include "snapshot.h"
#include "vk_context.h"

namespace tar {

struct RenderConfig {
  int width = 512;
  int height = 512;
  int frames = 6;
  int grid = 4;  // objects = grid * grid, each (width/grid) x (height/grid)
  PagingConfig paging;
  std::vector<int> inject_fault_objs;  // simulate per-object upload failures
  std::string shader_dir;
  std::string evidence_dir;  // frame PNGs are written here
  std::string label;         // run tag, e.g. "v1" / "v2"
};

// Renders the texture-array paging workload offscreen and records a
// version-neutral Snapshot of draw calls, resource states, page events
// and framebuffer captures.
class Renderer {
 public:
  Renderer(VkContext& ctx, const RenderConfig& cfg, Log& log);
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  Snapshot Run();

 private:
  void CreateRenderTarget();
  void CreateTextureArray();
  void CreatePipeline();
  void RecordPageUpload(VkCommandBuffer cmd, int layer, int page);
  void ReleaseStaging();
  Image Readback();

  VkContext& ctx_;
  RenderConfig cfg_;
  Log& log_;

  VkImage color_ = VK_NULL_HANDLE;
  VkDeviceMemory color_mem_ = VK_NULL_HANDLE;
  VkImageView color_view_ = VK_NULL_HANDLE;
  VkRenderPass render_pass_clear_ = VK_NULL_HANDLE;
  VkRenderPass render_pass_load_ = VK_NULL_HANDLE;
  VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

  VkImage array_ = VK_NULL_HANDLE;
  VkDeviceMemory array_mem_ = VK_NULL_HANDLE;
  VkImageView array_view_ = VK_NULL_HANDLE;
  VkSampler sampler_ = VK_NULL_HANDLE;

  VkDescriptorSetLayout desc_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet desc_set_ = VK_NULL_HANDLE;
  VkPipelineLayout pipe_layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;

  std::vector<std::pair<VkBuffer, VkDeviceMemory>> staging_;  // per-frame
};

}  // namespace tar
