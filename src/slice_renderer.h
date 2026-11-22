#pragma once

#include "common.h"
#include "evidence.h"
#include "vulkan_context.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>

namespace vox {

struct PushConstants {
  float width;
  float height;
  float slice;
  float pad;
};

struct DrawCallRecord {
  uint32_t slice_index = 0;
  uint32_t vertex_count = 3;
  VkImageLayout texture_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageLayout color_layout_before = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageLayout color_layout_after = VK_IMAGE_LAYOUT_UNDEFINED;
  std::string fingerprint;
};

struct RenderedSlice {
  uint32_t slice = 0;
  std::vector<uint8_t> rgba; // width*height*4
  std::string fingerprint;
};

// All GPU resources created for one snapshot; destroyed together when the
// snapshot's evidence has been captured.
struct SnapshotGpuResources {
  VkImage texture = VK_NULL_HANDLE;
  VkDeviceMemory texture_memory = VK_NULL_HANDLE;
  VkImageView texture_view = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  VkImage color = VK_NULL_HANDLE;
  VkDeviceMemory color_memory = VK_NULL_HANDLE;
  VkImageView color_view = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory staging_memory = VK_NULL_HANDLE;
  VkDeviceSize staging_size = 0;
  VkImageLayout texture_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageLayout color_layout = VK_IMAGE_LAYOUT_UNDEFINED;

  void destroy(VkDevice device) noexcept;
};

class SliceRenderer {
public:
  SliceRenderer(VulkanContext& ctx, EvidenceLog& log);
  ~SliceRenderer();

  // Renders every z-slice of the snapshot and returns the readback pixels.
  // All state changes and draw calls are written to the evidence log.
  std::vector<RenderedSlice> render_snapshot(const Snapshot& snap,
                                            const std::string& tag);

private:
  // Returns false when the driver cannot compile the graphics pipeline; the
  // caller then uses the CPU rasterization fallback (still writing into the
  // same Vulkan color image and reading it back through Vulkan).
  bool create_pipeline(SnapshotGpuResources& r);
  void upload_texture(SnapshotGpuResources& r, const Snapshot& snap);
  void create_color_target(SnapshotGpuResources& r);
  std::vector<uint8_t> readback(SnapshotGpuResources& r);
  std::vector<uint8_t> rasterize_cpu(const Snapshot& snap, uint32_t slice);
  void write_color_image(SnapshotGpuResources& r,
                         const std::vector<uint8_t>& rgba);
  void transition(VkCommandBuffer cmd, VkImage image,
                  VkImageLayout from, VkImageLayout to,
                  VkImageAspectFlags aspect, uint32_t layers);

  VulkanContext& ctx_;
  EvidenceLog& log_;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
};

} // namespace vox
