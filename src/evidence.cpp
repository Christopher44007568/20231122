#include "gbi/evidence.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace gbi {

namespace {

constexpr std::uint64_t kRawAlignment = 256;

std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment) {
  return (value + alignment - 1) / alignment * alignment;
}

Json stampJson(const TimeStamp& stamp) {
  Json result = Json::object();
  result["utc"] = stamp.utc;
  result["unix_ms"] = stamp.unix_ms;
  result["monotonic_ns"] = stamp.monotonic_ns;
  return result;
}

TimeStamp stampFromJson(const Json& json) {
  TimeStamp stamp;
  stamp.utc = json.value("utc");
  stamp.unix_ms = json.i64("unix_ms");
  stamp.monotonic_ns = json.u64("monotonic_ns");
  return stamp;
}

Json rangeJson(const SourceRange& range) {
  Json result = Json::object();
  result["offset"] = range.offset;
  result["length"] = range.length;
  return result;
}

SourceRange rangeFromJson(const Json& json) {
  SourceRange range;
  range.offset = json.u64("offset");
  range.length = json.u64("length");
  return range;
}

Json bboxJson(const BoundingBox& bounds) {
  Json result = Json::object();
  result["visible"] = bounds.visible;
  result["x"] = bounds.x;
  result["y"] = bounds.y;
  result["width"] = bounds.width;
  result["height"] = bounds.height;
  result["pixel_count"] = bounds.pixel_count;
  return result;
}

BoundingBox bboxFromJson(const Json& json) {
  BoundingBox bounds;
  bounds.visible = json.boolean("visible");
  bounds.x = static_cast<std::uint32_t>(json.u64("x"));
  bounds.y = static_cast<std::uint32_t>(json.u64("y"));
  bounds.width = static_cast<std::uint32_t>(json.u64("width"));
  bounds.height = static_cast<std::uint32_t>(json.u64("height"));
  bounds.pixel_count = static_cast<std::uint32_t>(json.u64("pixel_count"));
  return bounds;
}

Json attachmentJson(const AttachmentEvidence& attachment) {
  Json result = Json::object();
  result["name"] = attachment.name;
  result["format"] = attachment.format;
  result["width"] = attachment.width;
  result["height"] = attachment.height;
  result["pixel_stride"] = attachment.pixel_stride;
  result["row_stride"] = attachment.row_stride;
  result["offset"] = attachment.offset;
  result["length"] = attachment.length;
  result["sha256"] = attachment.sha256;
  return result;
}

AttachmentEvidence attachmentFromJson(const Json& json) {
  AttachmentEvidence attachment;
  attachment.name = json.value("name");
  attachment.format = json.value("format");
  attachment.width =
      static_cast<std::uint32_t>(json.u64("width"));
  attachment.height =
      static_cast<std::uint32_t>(json.u64("height"));
  attachment.pixel_stride =
      static_cast<std::uint32_t>(json.u64("pixel_stride"));
  attachment.row_stride =
      static_cast<std::uint32_t>(json.u64("row_stride"));
  attachment.offset = json.u64("offset");
  attachment.length = json.u64("length");
  attachment.sha256 = json.value("sha256");
  return attachment;
}

Json probeJson(const ProbePixel& probe) {
  Json result = Json::object();
  result["label"] = probe.label;
  result["x"] = probe.x;
  result["y"] = probe.y;
  result["attachment"] = probe.attachment;
  result["raw_offset"] = probe.raw_offset;
  result["byte_count"] = probe.byte_count;
  result["value_hex"] = probe.value_hex;
  return result;
}

ProbePixel probeFromJson(const Json& json) {
  ProbePixel probe;
  probe.label = json.value("label");
  probe.x = static_cast<std::uint32_t>(json.u64("x"));
  probe.y = static_cast<std::uint32_t>(json.u64("y"));
  probe.attachment = json.value("attachment");
  probe.raw_offset = json.u64("raw_offset");
  probe.byte_count =
      static_cast<std::uint32_t>(json.u64("byte_count"));
  probe.value_hex = json.value("value_hex");
  return probe;
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> choosePixels(
    const RenderMask& mask, std::uint32_t object_index, std::uint32_t width,
    const BoundingBox& bounds) {
  std::vector<std::pair<std::uint32_t, std::uint32_t>> points;
  if (!bounds.visible || bounds.pixel_count == 0) {
    return points;
  }
  const double target_x =
      static_cast<double>(bounds.x) + static_cast<double>(bounds.width) * 0.5;
  const double target_y =
      static_cast<double>(bounds.y) + static_cast<double>(bounds.height) * 0.5;
  std::pair<std::uint32_t, std::uint32_t> nearest{bounds.x, bounds.y};
  std::pair<std::uint32_t, std::uint32_t> first{bounds.x, bounds.y};
  bool have_first = false;
  double nearest_distance = std::numeric_limits<double>::max();
  const std::uint32_t max_y = bounds.y + bounds.height;
  const std::uint32_t max_x = bounds.x + bounds.width;
  for (std::uint32_t y = bounds.y; y < max_y; ++y) {
    for (std::uint32_t x = bounds.x; x < max_x; ++x) {
      const std::uint32_t value =
          mask.ids[static_cast<std::size_t>(y) * width + x];
      const bool matches =
          object_index == 0 ? value != 0 : value == object_index;
      if (!matches) {
        continue;
      }
      if (!have_first) {
        first = {x, y};
        have_first = true;
      }
      const double dx = static_cast<double>(x) - target_x;
      const double dy = static_cast<double>(y) - target_y;
      const double distance = dx * dx + dy * dy;
      if (distance < nearest_distance) {
        nearest_distance = distance;
        nearest = {x, y};
      }
    }
  }
  if (have_first) {
    points.push_back(first);
    if (nearest != first) {
      points.push_back(nearest);
    }
  }
  return points;
}

}  // namespace

RenderedAttachment makeAttachment(const std::string& name,
                                  const std::string& format,
                                  std::uint32_t width, std::uint32_t height,
                                  std::uint32_t pixel_stride,
                                  std::uint32_t row_stride, Bytes bytes) {
  RenderedAttachment attachment;
  attachment.name = name;
  attachment.format = format;
  attachment.width = width;
  attachment.height = height;
  attachment.pixel_stride = pixel_stride;
  attachment.row_stride = row_stride;
  attachment.bytes = std::move(bytes);
  return attachment;
}

BoundingBox computeBounds(const RenderMask& mask, std::uint32_t object_index,
                          std::uint32_t width, std::uint32_t height) {
  BoundingBox bounds;
  std::uint32_t min_x = width;
  std::uint32_t min_y = height;
  std::uint32_t max_x = 0;
  std::uint32_t max_y = 0;
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const std::uint32_t value =
          mask.ids[static_cast<std::size_t>(y) * width + x];
      const bool matches =
          object_index == 0 ? value != 0 : value == object_index;
      if (!matches) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
      ++bounds.pixel_count;
    }
  }
  if (bounds.pixel_count == 0) {
    return bounds;
  }
  bounds.visible = true;
  bounds.x = min_x;
  bounds.y = min_y;
  bounds.width = max_x - min_x + 1;
  bounds.height = max_y - min_y + 1;
  return bounds;
}

std::vector<ProbePixel> buildProbes(
    const std::vector<RenderedAttachment>& raw, const RenderMask& mask,
    std::uint32_t object_index, const BoundingBox& bounds,
    const std::vector<AttachmentEvidence>& packed) {
  std::vector<ProbePixel> probes;
  if (raw.empty() || packed.size() != raw.size()) {
    return probes;
  }
  std::uint32_t width = 0;
  for (const auto& attachment : raw) {
    if (attachment.width > 0) {
      width = attachment.width;
      break;
    }
  }
  const auto pixels = choosePixels(mask, object_index, width, bounds);
  for (std::size_t point_index = 0; point_index < pixels.size();
       ++point_index) {
    const auto [x, y] = pixels[point_index];
    const std::string point_label =
        point_index == 0 ? "first_visible" : "bbox_center_nearest";
    for (std::size_t attachment_index = 0; attachment_index < raw.size();
         ++attachment_index) {
      const RenderedAttachment& attachment = raw[attachment_index];
      const AttachmentEvidence& attachment_info = packed[attachment_index];
      const std::uint64_t local_offset =
          static_cast<std::uint64_t>(y) * attachment.row_stride +
          static_cast<std::uint64_t>(x) * attachment.pixel_stride;
      const std::uint64_t absolute_offset = attachment_info.offset + local_offset;
      if (local_offset + attachment.pixel_stride > attachment.bytes.size()) {
        continue;
      }
      ProbePixel probe;
      probe.label = point_label + "_" + attachment.name;
      probe.x = x;
      probe.y = y;
      probe.attachment = attachment.name;
      probe.raw_offset = absolute_offset;
      probe.byte_count = attachment.pixel_stride;
      probe.value_hex = hexEncode(attachment.bytes.data() + local_offset,
                                  attachment.pixel_stride);
      probes.push_back(std::move(probe));
    }
  }
  return probes;
}

ViewEvidence finalizeView(const RenderedView& rendered, const SceneObject& object,
                          const std::filesystem::path& output_root,
                          const std::string& raw_relative_path) {
  if (rendered.attachments.empty()) {
    throw std::runtime_error("cannot finalize a view with no attachments");
  }
  Bytes packed_bytes;
  std::vector<AttachmentEvidence> packed;
  packed.reserve(rendered.attachments.size());
  for (const RenderedAttachment& attachment : rendered.attachments) {
    const std::uint64_t offset = alignUp(packed_bytes.size(), kRawAlignment);
    if (packed_bytes.size() < offset) {
      packed_bytes.resize(static_cast<std::size_t>(offset), 0);
    }
    const std::size_t expected =
        static_cast<std::size_t>(attachment.row_stride) * attachment.height;
    if (attachment.bytes.size() < expected) {
      throw std::runtime_error("attachment is shorter than its row layout: " +
                               attachment.name);
    }
    AttachmentEvidence evidence;
    evidence.name = attachment.name;
    evidence.format = attachment.format;
    evidence.width = attachment.width;
    evidence.height = attachment.height;
    evidence.pixel_stride = attachment.pixel_stride;
    evidence.row_stride = attachment.row_stride;
    evidence.offset = offset;
    evidence.length = attachment.bytes.size();
    evidence.sha256 = sha256Hex(attachment.bytes);
    packed_bytes.insert(packed_bytes.end(), attachment.bytes.begin(),
                        attachment.bytes.end());
    packed.push_back(std::move(evidence));
  }
  const std::uint64_t aligned_size = alignUp(packed_bytes.size(), kRawAlignment);
  packed_bytes.resize(static_cast<std::size_t>(aligned_size), 0);

  const std::filesystem::path raw_path = output_root / raw_relative_path;
  writeFile(raw_path, packed_bytes);

  ViewEvidence view;
  view.view_id = rendered.view_id;
  view.kind = rendered.kind;
  view.object_id = rendered.object_id;
  view.object_index = rendered.object_index;
  view.rendered_at = rendered.rendered_at;
  view.frame_index = rendered.frame_index;
  view.width = rendered.width;
  view.height = rendered.height;
  view.bounds = computeBounds(rendered.mask, rendered.object_index,
                              rendered.width, rendered.height);
  view.raw_file = raw_relative_path;
  view.raw_size = packed_bytes.size();
  view.raw_sha256 = sha256Hex(packed_bytes);
  view.attachments = packed;
  view.probes = buildProbes(rendered.attachments, rendered.mask,
                            rendered.object_index, view.bounds, packed);
  view.source_range = object.source_range;
  view.corrected = object.corrected;
  view.overlay_path = object.overlay_path;
  view.overlay_sha256 = object.overlay_sha256;
  if (object.overlay_albedo_range.length > 0) {
    view.overlay_material_range = object.overlay_albedo_range;
  } else if (object.overlay_roughness_range.length > 0) {
    view.overlay_material_range = object.overlay_roughness_range;
  } else {
    view.overlay_material_range = object.overlay_metallic_range;
  }
  Json material = Json::object();
  Json albedo = Json::array();
  albedo.push(static_cast<double>(object.albedo.x));
  albedo.push(static_cast<double>(object.albedo.y));
  albedo.push(static_cast<double>(object.albedo.z));
  material["albedo"] = albedo;
  material["roughness"] = static_cast<double>(object.roughness);
  material["metallic"] = static_cast<double>(object.metallic);
  view.material = material;
  return view;
}

Bytes parseHex(const std::string& value) {
  if (value.size() % 2 != 0) {
    throw std::runtime_error("hex value has odd length");
  }
  Bytes bytes(value.size() / 2);
  const auto nibble = [](char character) -> std::uint8_t {
    if (character >= '0' && character <= '9') {
      return static_cast<std::uint8_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
      return static_cast<std::uint8_t>(character - 'a' + 10);
    }
    if (character >= 'A' && character <= 'F') {
      return static_cast<std::uint8_t>(character - 'A' + 10);
    }
    throw std::runtime_error("invalid hex digit");
  };
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::uint8_t>(
        (nibble(value[index * 2]) << 4U) | nibble(value[index * 2 + 1]));
  }
  return bytes;
}

std::string hexFromBytes(const Bytes& value) {
  return hexEncode(value);
}

namespace {

Json viewToJson(const ViewEvidence& view) {
  Json result = Json::object();
  result["view_id"] = view.view_id;
  result["kind"] = view.kind;
  result["object_id"] = view.object_id;
  result["object_index"] = view.object_index;
  result["rendered_at"] = stampJson(view.rendered_at);
  result["frame_index"] = view.frame_index;
  result["width"] = view.width;
  result["height"] = view.height;
  result["bounds"] = bboxJson(view.bounds);
  result["raw_file"] = view.raw_file;
  result["raw_size"] = view.raw_size;
  result["raw_sha256"] = view.raw_sha256;
  Json attachments = Json::array();
  for (const AttachmentEvidence& attachment : view.attachments) {
    attachments.push(attachmentJson(attachment));
  }
  result["attachments"] = attachments;
  Json probes = Json::array();
  for (const ProbePixel& probe : view.probes) {
    probes.push(probeJson(probe));
  }
  result["probes"] = probes;
  result["source_range"] = rangeJson(view.source_range);
  Json overlay = Json::object();
  overlay["corrected"] = view.corrected;
  overlay["path"] = view.overlay_path;
  overlay["sha256"] = view.overlay_sha256;
  overlay["material_range"] = rangeJson(view.overlay_material_range);
  result["overlay"] = overlay;
  result["material"] = view.material;
  return result;
}

ViewEvidence viewFromJson(const Json& json) {
  ViewEvidence view;
  view.view_id = json.value("view_id");
  view.kind = json.value("kind");
  view.object_id = json.value("object_id");
  view.object_index =
      static_cast<std::uint32_t>(json.u64("object_index"));
  view.rendered_at = stampFromJson(json.at("rendered_at"));
  view.frame_index = json.u64("frame_index");
  view.width = static_cast<std::uint32_t>(json.u64("width"));
  view.height = static_cast<std::uint32_t>(json.u64("height"));
  view.bounds = bboxFromJson(json.at("bounds"));
  view.raw_file = json.value("raw_file");
  view.raw_size = json.u64("raw_size");
  view.raw_sha256 = json.value("raw_sha256");
  if (json.contains("attachments")) {
    for (const Json& item : json.at("attachments").asArray()) {
      view.attachments.push_back(attachmentFromJson(item));
    }
  }
  if (json.contains("probes")) {
    for (const Json& item : json.at("probes").asArray()) {
      view.probes.push_back(probeFromJson(item));
    }
  }
  if (json.contains("source_range")) {
    view.source_range = rangeFromJson(json.at("source_range"));
  }
  if (json.contains("overlay")) {
    const Json& overlay_json = json.at("overlay");
    view.corrected = overlay_json.boolean("corrected");
    view.overlay_path = overlay_json.value("path");
    view.overlay_sha256 = overlay_json.value("sha256");
    if (overlay_json.contains("material_range")) {
      view.overlay_material_range =
          rangeFromJson(overlay_json.at("material_range"));
    }
  }
  if (json.contains("material")) {
    view.material = json.at("material");
  }
  return view;
}

}  // namespace

std::string CaptureEvidence::toJson() const {
  Json root = Json::object();
  root["schema"] = schema;
  root["capture_id"] = capture_id;
  root["anchor"] = anchor;
  root["scope"] = scope;
  root["object_filter"] = object_filter;
  root["revision"] = revision;
  root["note"] = note;
  root["started_at"] = stampJson(started_at);
  root["completed_at"] = stampJson(completed_at);
  root["generation"] = generation;
  root["width"] = width;
  root["height"] = height;
  root["scene_source"] = scene_source;
  root["scene_sha256"] = scene_sha256;
  root["scene_fingerprint"] = scene_fingerprint;
  root["original_sha256"] = original_sha256;
  root["originals_verified_at"] = originals_verified_at;
  root["vulkan_device"] = vulkan_device;
  root["vulkan_driver"] = vulkan_driver;
  Json view_array = Json::array();
  for (const ViewEvidence& view : views) {
    view_array.push(viewToJson(view));
  }
  root["views"] = view_array;
  root["extra"] = extra;
  return root.dump(2);
}

CaptureEvidence CaptureEvidence::fromJson(const Json& json) {
  CaptureEvidence capture;
  capture.schema = json.value("schema", capture.schema);
  capture.capture_id = json.value("capture_id");
  capture.anchor = json.value("anchor");
  capture.scope = json.value("scope");
  capture.object_filter = json.value("object_filter");
  capture.revision = json.value("revision");
  capture.note = json.value("note");
  capture.started_at = stampFromJson(json.at("started_at"));
  capture.completed_at = stampFromJson(json.at("completed_at"));
  capture.generation = json.u64("generation");
  capture.width = static_cast<std::uint32_t>(json.u64("width"));
  capture.height = static_cast<std::uint32_t>(json.u64("height"));
  capture.scene_source = json.value("scene_source");
  capture.scene_sha256 = json.value("scene_sha256");
  capture.scene_fingerprint = json.value("scene_fingerprint");
  capture.original_sha256 = json.value("original_sha256");
  capture.originals_verified_at = json.value("originals_verified_at");
  capture.vulkan_device = json.value("vulkan_device");
  capture.vulkan_driver = json.value("vulkan_driver");
  if (capture.capture_id.empty() ||
      capture.capture_id.find('/') != std::string::npos ||
      capture.capture_id.find('\\') != std::string::npos ||
      capture.capture_id.find("..") != std::string::npos) {
    throw std::runtime_error("capture manifest has an unsafe capture_id");
  }
  if (capture.anchor != "A0" && capture.anchor != "A1" &&
      capture.anchor != "A2") {
    throw std::runtime_error("capture manifest has an unknown anchor");
  }
  if (json.contains("views")) {
    for (const Json& item : json.at("views").asArray()) {
      capture.views.push_back(viewFromJson(item));
    }
  }
  if (json.contains("extra")) {
    capture.extra = json.at("extra");
  }
  return capture;
}

const ViewEvidence* CaptureEvidence::findView(
    const std::string& kind, const std::string& object_id) const {
  for (const ViewEvidence& view : views) {
    if (view.kind != kind) {
      continue;
    }
    if (kind == "scene" || view.object_id == object_id) {
      return &view;
    }
  }
  return nullptr;
}

}  // namespace gbi
