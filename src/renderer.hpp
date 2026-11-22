#pragma once

#include "format.hpp"
#include "image.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace msphere {

struct RenderOptions {
  uint32_t width = 960;
  uint32_t height = 540;
  uint32_t anomaly_instance = 5;
  float anomaly_red = 0.025F;
};

struct GpuTiming {
  bool valid = false;
  uint64_t start_ticks = 0;
  uint64_t end_ticks = 0;
  double elapsed_ns = 0.0;
};

struct DrawCallEvidence {
  uint64_t sequence = 0;
  std::string frame;
  uint32_t first_index = 0;
  uint32_t index_count = 0;
  int32_t vertex_offset = 0;
  uint32_t first_instance = 0;
  uint32_t instance_count = 0;
  uint64_t command_buffer = 0;
  uint64_t framebuffer = 0;
  uint64_t color_image = 0;
  uint64_t depth_image = 0;
  uint64_t material_buffer = 0;
  uint64_t instance_buffer = 0;
  uint64_t material_byte_offset = 0;
  uint64_t instance_byte_offset = 0;
  std::string material_content_sha256;
  GpuTiming gpu;
};

struct BufferEvidence {
  BufferEvidence() = default;
  BufferEvidence(std::string evidence_name, uint64_t buffer_handle,
                 uint64_t buffer_size, uint64_t buffer_usage,
                 std::string content_hash)
      : name(std::move(evidence_name)),
        handle(buffer_handle),
        size(buffer_size),
        usage(buffer_usage),
        content_sha256(std::move(content_hash)) {}

  std::string name;
  uint64_t handle = 0;
  uint64_t size = 0;
  uint64_t usage = 0;
  std::string content_sha256;
};

struct ImageEvidence {
  ImageEvidence() = default;
  ImageEvidence(std::string evidence_name, uint64_t image_handle,
                uint64_t image_view, uint32_t image_width,
                uint32_t image_height, std::string image_format,
                uint64_t image_usage, std::string before_layout,
                std::string after_layout,
                std::string transition_log = "")
      : name(std::move(evidence_name)),
        handle(image_handle),
        view(image_view),
        width(image_width),
        height(image_height),
        format(std::move(image_format)),
        usage(image_usage),
        layout_before(std::move(before_layout)),
        layout_after(std::move(after_layout)),
        transitions(std::move(transition_log)) {}

  std::string name;
  uint64_t handle = 0;
  uint64_t view = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  std::string format;
  uint64_t usage = 0;
  std::string layout_before;
  std::string layout_after;
  std::string transitions;
};

struct AnomalyEvidence {
  uint32_t instance_index = 0;
  uint32_t material_index = 0;
  uint32_t material_count = 0;
  std::string field;
  uint64_t source_file_offset = 0;
  float original_value = 0.0F;
  float mutated_value = 0.0F;
  uint32_t original_bits = 0;
  uint32_t mutated_bits = 0;
  std::string original_bytes_hex;
  std::string mutated_bytes_hex;
};

struct RendererInfo {
  std::string glfw_version;
  std::string glfw_platform;
  std::string vulkan_api_version;
  std::string device_name;
  std::string driver_name;
  uint32_t vendor_id = 0;
  uint32_t device_id = 0;
  uint32_t queue_family_index = 0;
  float timestamp_period_ns = 0.0F;
  bool timestamp_supported = false;
  std::string color_format;
  uint32_t frame_count = 0;
};

struct InstancePlacement {
  uint32_t index = 0;
  float world_x = 0.0F;
  float world_y = 0.0F;
  uint32_t x0 = 0;
  uint32_t y0 = 0;
  uint32_t x1 = 0;
  uint32_t y1 = 0;
};

struct FrameEvidence {
  std::string name;
  RgbaImage image;
  DrawCallEvidence draw;
};

struct RenderResult {
  bool ok = false;
  std::string error;
  RendererInfo info;
  std::vector<FrameEvidence> frames;
  std::vector<BufferEvidence> buffers;
  std::vector<ImageEvidence> images;
  std::vector<InstancePlacement> placements;
  AnomalyEvidence anomaly;
};

RenderResult render_frames(const ParsedInput& input,
                           const RenderOptions& options);

}  // namespace msphere
