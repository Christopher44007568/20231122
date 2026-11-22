#pragma once

#include "gbi/scene.hpp"
#include "gbi/util.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gbi {

struct BoundingBox {
  bool visible = false;
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t pixel_count = 0;
};

struct ProbePixel {
  std::string label;
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::string attachment;
  std::uint64_t raw_offset = 0;
  std::uint32_t byte_count = 0;
  std::string value_hex;
};

struct AttachmentEvidence {
  std::string name;
  std::string format;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t pixel_stride = 0;
  std::uint32_t row_stride = 0;
  std::uint64_t offset = 0;
  std::uint64_t length = 0;
  std::string sha256;
};

struct ViewEvidence {
  std::string view_id;
  std::string kind;
  std::string object_id;
  std::uint32_t object_index = 0;
  TimeStamp rendered_at;
  std::uint64_t frame_index = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  BoundingBox bounds;
  std::string raw_file;
  std::uint64_t raw_size = 0;
  std::string raw_sha256;
  std::vector<AttachmentEvidence> attachments;
  std::vector<ProbePixel> probes;
  SourceRange source_range;
  bool corrected = false;
  std::string overlay_path;
  std::string overlay_sha256;
  SourceRange overlay_material_range;
  Json material = Json::object();
};

struct CaptureEvidence {
  std::string schema = "gbi.capture/1";
  std::string capture_id;
  std::string anchor;
  std::string scope;
  std::string object_filter;
  std::string revision;
  std::string note;
  TimeStamp started_at;
  TimeStamp completed_at;
  std::uint64_t generation = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::string scene_source;
  std::string scene_sha256;
  std::string scene_fingerprint;
  std::string original_sha256;
  std::string originals_verified_at;
  std::string vulkan_device;
  std::string vulkan_driver;
  std::vector<ViewEvidence> views;
  Json extra = Json::object();

  std::string toJson() const;
  static CaptureEvidence fromJson(const Json& json);
  const ViewEvidence* findView(const std::string& kind,
                               const std::string& object_id = {}) const;
};

struct RenderedAttachment {
  std::string name;
  std::string format;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t pixel_stride = 0;
  std::uint32_t row_stride = 0;
  Bytes bytes;
};

struct RenderMask {
  std::vector<std::uint32_t> ids;
};

struct RenderedView {
  std::string view_id;
  std::string kind;
  std::string object_id;
  std::uint32_t object_index = 0;
  TimeStamp rendered_at;
  std::uint64_t frame_index = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<RenderedAttachment> attachments;
  RenderMask mask;
};

RenderedAttachment makeAttachment(const std::string& name,
                                  const std::string& format,
                                  std::uint32_t width, std::uint32_t height,
                                  std::uint32_t pixel_stride,
                                  std::uint32_t row_stride, Bytes bytes);

BoundingBox computeBounds(const RenderMask& mask, std::uint32_t object_index,
                          std::uint32_t width, std::uint32_t height);
std::vector<ProbePixel> buildProbes(const std::vector<RenderedAttachment>& raw,
                                    const RenderMask& mask,
                                    std::uint32_t object_index,
                                    const BoundingBox& bounds,
                                    const std::vector<AttachmentEvidence>& packed);

ViewEvidence finalizeView(const RenderedView& rendered, const SceneObject& object,
                          const std::filesystem::path& output_root,
                          const std::string& raw_relative_path);
Bytes parseHex(const std::string& value);
std::string hexFromBytes(const Bytes& value);

}  // namespace gbi
