#include "gbi/compare.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace gbi {

namespace {

const AttachmentEvidence* findAttachment(const ViewEvidence& view,
                                         const std::string& name) {
  for (const AttachmentEvidence& attachment : view.attachments) {
    if (attachment.name == name) {
      return &attachment;
    }
  }
  return nullptr;
}

const ViewEvidence* requireObjectView(const CaptureEvidence& capture,
                                      const std::string& object_id) {
  const ViewEvidence* view = capture.findView("object", object_id);
  if (view == nullptr) {
    throw std::runtime_error("capture " + capture.capture_id +
                             " has no isolated view for object " + object_id);
  }
  return view;
}

std::string normalizeAnchor(const std::string& value) {
  const std::string lowered = toLower(value);
  if (lowered == "a0") {
    return "A0";
  }
  if (lowered == "a1") {
    return "A1";
  }
  if (lowered == "a2") {
    return "A2";
  }
  return value;
}

std::uint16_t readU16(const Bytes& data, std::size_t offset) {
  return static_cast<std::uint16_t>(data[offset]) |
         static_cast<std::uint16_t>(data[offset + 1] << 8U);
}

float halfToFloat(std::uint16_t value) {
  const std::uint32_t sign = (value >> 15U) & 1U;
  const std::uint32_t exponent = (value >> 10U) & 0x1fU;
  const std::uint32_t mantissa = value & 0x3ffU;
  float result = 0.0F;
  if (exponent == 0) {
    result = std::ldexp(static_cast<float>(mantissa), -24);
  } else if (exponent == 31) {
    result = mantissa == 0 ? std::numeric_limits<float>::infinity()
                           : std::numeric_limits<float>::quiet_NaN();
  } else {
    result = std::ldexp(static_cast<float>(mantissa + 1024),
                        static_cast<int>(exponent) - 25);
  }
  return sign != 0U ? -result : result;
}

std::uint8_t clampByte(double value) {
  return static_cast<std::uint8_t>(
      std::clamp(std::lround(value), 0L, 255L));
}

void pixelRgb(const AttachmentEvidence& attachment, const Bytes& raw,
              std::uint32_t x, std::uint32_t y, std::uint8_t& red,
              std::uint8_t& green, std::uint8_t& blue,
              const std::string& mode) {
  const std::uint64_t offset =
      attachment.offset + static_cast<std::uint64_t>(y) * attachment.row_stride +
      static_cast<std::uint64_t>(x) * attachment.pixel_stride;
  if (offset + attachment.pixel_stride > raw.size()) {
    red = green = blue = 0;
    return;
  }
  const std::string& format = attachment.format;
  if (format == "R8G8B8A8_UNORM" || format == "R8G8B8A8_SRGB") {
    red = raw[offset];
    green = raw[offset + 1];
    blue = raw[offset + 2];
  } else if (format == "R32_UINT") {
    std::uint32_t id = 0;
    std::memcpy(&id, raw.data() + offset, sizeof(id));
    const std::uint32_t hash = id * 2654435761U;
    red = static_cast<std::uint8_t>(64U + (hash & 0x7fU));
    green = static_cast<std::uint8_t>(64U + ((hash >> 8U) & 0x7fU));
    blue = static_cast<std::uint8_t>(64U + ((hash >> 16U) & 0x7fU));
  } else if (format == "D32_SFLOAT") {
    float depth = 0.0F;
    std::memcpy(&depth, raw.data() + offset, sizeof(depth));
    const std::uint8_t value =
        clampByte(255.0 * (1.0 - std::clamp(depth, 0.0F, 1.0F)));
    red = green = blue = value;
  } else if (format == "R16G16B16A16_SFLOAT") {
    const float px = halfToFloat(readU16(raw, offset));
    const float py = halfToFloat(readU16(raw, offset + 2));
    const float pz = halfToFloat(readU16(raw, offset + 4));
    red = clampByte((px * 0.5 + 0.5) * 255.0);
    green = clampByte((py * 0.5 + 0.5) * 255.0);
    blue = clampByte((pz * 0.5 + 0.5) * 255.0);
  } else {
    red = green = blue = raw[offset];
  }
  if (mode == "normal") {
    red = clampByte(red * 1.2);
    green = clampByte(green * 1.2);
    blue = clampByte(blue * 1.2);
  }
}

}  // namespace

Bytes readViewRaw(const fs::path& capture_dir, const ViewEvidence& view) {
  if (view.raw_file.empty() || fs::path(view.raw_file).is_absolute() ||
      view.raw_file.find("..") != std::string::npos) {
    throw std::runtime_error("unsafe raw view path: " + view.raw_file);
  }
  const fs::path path = capture_dir / view.raw_file;
  Bytes bytes = readFile(path);
  if (bytes.size() != view.raw_size) {
    throw std::runtime_error("raw size mismatch: " + path.string());
  }
  if (sha256Hex(bytes) != view.raw_sha256) {
    throw std::runtime_error("raw hash mismatch: " + path.string());
  }
  return bytes;
}

void writePpm(const fs::path& path, const ViewEvidence&,
              const AttachmentEvidence& attachment, const Bytes& raw,
              const std::string& mode) {
  ensureDir(path.parent_path());
  std::string output;
  output.reserve(static_cast<std::size_t>(attachment.width) *
                     attachment.height * 3 +
                 64);
  output += "P6\n";
  output += std::to_string(attachment.width) + " " +
            std::to_string(attachment.height) + "\n255\n";
  for (std::uint32_t y = 0; y < attachment.height; ++y) {
    for (std::uint32_t x = 0; x < attachment.width; ++x) {
      std::uint8_t red = 0;
      std::uint8_t green = 0;
      std::uint8_t blue = 0;
      pixelRgb(attachment, raw, x, y, red, green, blue, mode);
      output.push_back(static_cast<char>(red));
      output.push_back(static_cast<char>(green));
      output.push_back(static_cast<char>(blue));
    }
  }
  writeText(path, output);
}

void writeDiffPpm(const fs::path& path, const ViewEvidence&,
                  const AttachmentEvidence& left_attachment,
                  const Bytes& left_raw, const ViewEvidence&,
                  const AttachmentEvidence& right_attachment,
                  const Bytes& right_raw) {
  ensureDir(path.parent_path());
  if (left_attachment.width != right_attachment.width ||
      left_attachment.height != right_attachment.height) {
    throw std::runtime_error("cannot diff attachments with different sizes");
  }
  std::string output;
  output += "P6\n";
  output += std::to_string(left_attachment.width) + " " +
            std::to_string(left_attachment.height) + "\n255\n";
  for (std::uint32_t y = 0; y < left_attachment.height; ++y) {
    for (std::uint32_t x = 0; x < left_attachment.width; ++x) {
      std::uint8_t left_rgb[3] = {};
      std::uint8_t right_rgb[3] = {};
      pixelRgb(left_attachment, left_raw, x, y, left_rgb[0], left_rgb[1],
               left_rgb[2], {});
      pixelRgb(right_attachment, right_raw, x, y, right_rgb[0], right_rgb[1],
               right_rgb[2], {});
      const int delta = std::max(
          {std::abs(static_cast<int>(left_rgb[0]) - right_rgb[0]),
           std::abs(static_cast<int>(left_rgb[1]) - right_rgb[1]),
           std::abs(static_cast<int>(left_rgb[2]) - right_rgb[2])});
      if (delta == 0) {
        const std::uint8_t gray = static_cast<std::uint8_t>(
            (right_rgb[0] + right_rgb[1] + right_rgb[2]) / 8);
        output.push_back(static_cast<char>(gray));
        output.push_back(static_cast<char>(gray));
        output.push_back(static_cast<char>(gray));
      } else {
        output.push_back(static_cast<char>(std::min(255, 80 + delta * 2)));
        output.push_back(static_cast<char>(std::max(0, 80 - delta)));
        output.push_back(static_cast<char>(std::max(0, 80 - delta / 2)));
      }
    }
  }
  writeText(path, output);
}

ObjectComparison compareObject(const Workspace& workspace,
                               const std::string& object_id,
                               const std::string& left_anchor,
                               const std::string& right_anchor,
                               const std::string& left_capture_id,
                               const std::string& right_capture_id) {
  const std::string left_anchor_name = normalizeAnchor(left_anchor);
  const std::string right_anchor_name = normalizeAnchor(right_anchor);
  const std::string resolved_left_id =
      left_capture_id.empty()
          ? workspace.latestCaptureId(left_anchor_name, object_id)
          : left_capture_id;
  const std::string resolved_right_id =
      right_capture_id.empty()
          ? workspace.latestCaptureId(right_anchor_name, object_id)
          : right_capture_id;
  const auto left_capture =
      workspace.loadCapture(left_anchor_name, resolved_left_id);
  const auto right_capture =
      workspace.loadCapture(right_anchor_name, resolved_right_id);
  if (!left_capture.has_value()) {
    throw std::runtime_error("left capture not found for anchor " +
                             left_anchor_name);
  }
  if (!right_capture.has_value()) {
    throw std::runtime_error("right capture not found for anchor " +
                             right_anchor_name);
  }
  const ViewEvidence* left_view =
      requireObjectView(*left_capture, object_id);
  const ViewEvidence* right_view =
      requireObjectView(*right_capture, object_id);
  if (left_view->width != right_view->width ||
      left_view->height != right_view->height) {
    throw std::runtime_error("object views have different dimensions");
  }
  const fs::path left_dir = workspace.captureDirectory(*left_capture);
  const fs::path right_dir = workspace.captureDirectory(*right_capture);
  const Bytes left_raw = readViewRaw(left_dir, *left_view);
  const Bytes right_raw = readViewRaw(right_dir, *right_view);

  ObjectComparison comparison;
  comparison.comparison_id =
      "cmp-" + nowStamp().utc + "-" + object_id + "-" + randomToken(3);
  comparison.object_id = object_id;
  comparison.left_capture_id = left_capture->capture_id;
  comparison.right_capture_id = right_capture->capture_id;
  comparison.left_anchor = left_capture->anchor;
  comparison.right_anchor = right_capture->anchor;
  comparison.left_view_id = left_view->view_id;
  comparison.right_view_id = right_view->view_id;
  comparison.left_captured_at = left_view->rendered_at;
  comparison.right_captured_at = right_view->rendered_at;
  comparison.left_raw_file = left_view->raw_file;
  comparison.right_raw_file = right_view->raw_file;
  comparison.left_raw_size = left_view->raw_size;
  comparison.right_raw_size = right_view->raw_size;
  comparison.left_raw_sha256 = left_view->raw_sha256;
  comparison.right_raw_sha256 = right_view->raw_sha256;
  comparison.source_range = right_view->source_range;
  comparison.corrected = right_view->corrected;
  comparison.overlay_path = right_view->overlay_path;
  comparison.overlay_sha256 = right_view->overlay_sha256;
  comparison.overlay_material_range = right_view->overlay_material_range;
  comparison.original_sha256 = right_capture->original_sha256;
  comparison.generated_at = nowStamp().utc;

  std::uint32_t changed_min_x = left_view->width;
  std::uint32_t changed_min_y = left_view->height;
  std::uint32_t changed_max_x = 0;
  std::uint32_t changed_max_y = 0;
  bool any_changed = false;

  for (const AttachmentEvidence& left_attachment : left_view->attachments) {
    const AttachmentEvidence* right_attachment =
        findAttachment(*right_view, left_attachment.name);
    if (right_attachment == nullptr) {
      throw std::runtime_error("right view is missing attachment " +
                               left_attachment.name);
    }
    if (left_attachment.length != right_attachment->length ||
        left_attachment.width != right_attachment->width ||
        left_attachment.height != right_attachment->height ||
        left_attachment.pixel_stride != right_attachment->pixel_stride) {
      throw std::runtime_error("attachment layouts differ for " +
                               left_attachment.name);
    }
    if (left_attachment.offset + left_attachment.length > left_raw.size() ||
        right_attachment->offset + right_attachment->length >
            right_raw.size()) {
      throw std::runtime_error("attachment exceeds raw file for " +
                               left_attachment.name);
    }
    AttachmentMetric metric;
    metric.attachment = left_attachment.name;
    metric.left_offset = left_attachment.offset;
    metric.right_offset = right_attachment->offset;
    metric.byte_length = left_attachment.length;
    metric.left_sha256 = left_attachment.sha256;
    metric.right_sha256 = right_attachment->sha256;
    metric.total_pixels =
        static_cast<std::uint64_t>(left_attachment.width) *
        left_attachment.height;
    std::uint64_t difference_sum = 0;
    for (std::size_t index = 0; index < left_attachment.length; ++index) {
      const std::uint32_t left_byte =
          left_raw[left_attachment.offset + index];
      const std::uint32_t right_byte =
          right_raw[right_attachment->offset + index];
      if (left_byte != right_byte) {
        ++metric.changed_bytes;
      }
      const std::uint32_t difference =
          left_byte > right_byte ? left_byte - right_byte
                                 : right_byte - left_byte;
      difference_sum += difference;
      metric.maximum_difference =
          std::max(metric.maximum_difference, difference);
    }
    metric.mean_absolute_difference =
        left_attachment.length == 0
            ? 0.0
            : static_cast<double>(difference_sum) /
                  static_cast<double>(left_attachment.length);
    for (std::uint32_t y = 0; y < left_attachment.height; ++y) {
      for (std::uint32_t x = 0; x < left_attachment.width; ++x) {
        const std::uint64_t local =
            static_cast<std::uint64_t>(y) * left_attachment.row_stride +
            static_cast<std::uint64_t>(x) * left_attachment.pixel_stride;
        bool changed = false;
        for (std::uint32_t channel = 0; channel < left_attachment.pixel_stride;
             ++channel) {
          if (left_raw[left_attachment.offset + local + channel] !=
              right_raw[right_attachment->offset + local + channel]) {
            changed = true;
            break;
          }
        }
        if (changed) {
          ++metric.changed_pixels;
          if (left_attachment.name == "albedo") {
            changed_min_x = std::min(changed_min_x, x);
            changed_min_y = std::min(changed_min_y, y);
            changed_max_x = std::max(changed_max_x, x);
            changed_max_y = std::max(changed_max_y, y);
            any_changed = true;
          }
        }
      }
    }
    comparison.metrics.push_back(std::move(metric));
  }
  if (any_changed) {
    comparison.changed_bounds.visible = true;
    comparison.changed_bounds.x = changed_min_x;
    comparison.changed_bounds.y = changed_min_y;
    comparison.changed_bounds.width = changed_max_x - changed_min_x + 1;
    comparison.changed_bounds.height = changed_max_y - changed_min_y + 1;
    comparison.changed_bounds.pixel_count = 0;
    for (const AttachmentMetric& metric : comparison.metrics) {
      if (metric.attachment == "albedo") {
        comparison.changed_bounds.pixel_count =
            static_cast<std::uint32_t>(metric.changed_pixels);
      }
    }
  }
  comparison.identical =
      std::all_of(comparison.metrics.begin(), comparison.metrics.end(),
                  [](const AttachmentMetric& metric) {
                    return metric.changed_bytes == 0;
                  });

  std::map<std::string, const ProbePixel*> left_probes;
  for (const ProbePixel& probe : left_view->probes) {
    left_probes[probe.label] = &probe;
  }
  for (const ProbePixel& right_probe : right_view->probes) {
    const auto iterator = left_probes.find(right_probe.label);
    if (iterator == left_probes.end()) {
      continue;
    }
    ProbeDelta delta;
    delta.label = right_probe.label;
    delta.attachment = right_probe.attachment;
    delta.x = right_probe.x;
    delta.y = right_probe.y;
    delta.left_offset = iterator->second->raw_offset;
    delta.right_offset = right_probe.raw_offset;
    delta.left_hex = iterator->second->value_hex;
    delta.right_hex = right_probe.value_hex;
    delta.changed = delta.left_hex != delta.right_hex;
    comparison.probes.push_back(std::move(delta));
  }
  return comparison;
}

std::string ObjectComparison::toJson() const {
  Json root = Json::object();
  root["schema"] = schema;
  root["comparison_id"] = comparison_id;
  root["object_id"] = object_id;
  root["verdict"] = identical ? "IDENTICAL" : "CHANGED";
  root["generated_at"] = generated_at;
  root["original_sha256"] = original_sha256;
  Json left = Json::object();
  left["anchor"] = left_anchor;
  left["capture_id"] = left_capture_id;
  left["view_id"] = left_view_id;
  left["captured_utc"] = left_captured_at.utc;
  left["captured_unix_ms"] = left_captured_at.unix_ms;
  left["raw_file"] = left_raw_file;
  left["raw_size"] = left_raw_size;
  left["raw_sha256"] = left_raw_sha256;
  root["left"] = left;
  Json right = Json::object();
  right["anchor"] = right_anchor;
  right["capture_id"] = right_capture_id;
  right["view_id"] = right_view_id;
  right["captured_utc"] = right_captured_at.utc;
  right["captured_unix_ms"] = right_captured_at.unix_ms;
  right["raw_file"] = right_raw_file;
  right["raw_size"] = right_raw_size;
  right["raw_sha256"] = right_raw_sha256;
  root["right"] = right;
  Json source = Json::object();
  source["file"] = "originals/scene.json";
  source["offset"] = source_range.offset;
  source["length"] = source_range.length;
  root["source_material"] = source;
  Json overlay = Json::object();
  overlay["corrected"] = corrected;
  overlay["path"] = overlay_path;
  overlay["sha256"] = overlay_sha256;
  overlay["material_offset"] = overlay_material_range.offset;
  overlay["material_length"] = overlay_material_range.length;
  root["overlay"] = overlay;
  Json bounds = Json::object();
  bounds["visible"] = changed_bounds.visible;
  bounds["x"] = changed_bounds.x;
  bounds["y"] = changed_bounds.y;
  bounds["width"] = changed_bounds.width;
  bounds["height"] = changed_bounds.height;
  bounds["pixel_count"] = changed_bounds.pixel_count;
  root["changed_bounds"] = bounds;
  Json metric_array = Json::array();
  for (const AttachmentMetric& metric : this->metrics) {
    Json item = Json::object();
    item["attachment"] = metric.attachment;
    item["left_offset"] = metric.left_offset;
    item["right_offset"] = metric.right_offset;
    item["byte_length"] = metric.byte_length;
    item["changed_bytes"] = metric.changed_bytes;
    item["changed_pixels"] = metric.changed_pixels;
    item["total_pixels"] = metric.total_pixels;
    item["mean_absolute_difference"] = metric.mean_absolute_difference;
    item["maximum_difference"] = metric.maximum_difference;
    item["left_sha256"] = metric.left_sha256;
    item["right_sha256"] = metric.right_sha256;
    metric_array.push(item);
  }
  root["attachments"] = metric_array;
  Json probe_array = Json::array();
  for (const ProbeDelta& probe : this->probes) {
    Json item = Json::object();
    item["label"] = probe.label;
    item["attachment"] = probe.attachment;
    item["x"] = probe.x;
    item["y"] = probe.y;
    item["left_offset"] = probe.left_offset;
    item["right_offset"] = probe.right_offset;
    item["left_hex"] = probe.left_hex;
    item["right_hex"] = probe.right_hex;
    item["changed"] = probe.changed;
    probe_array.push(item);
  }
  root["probes"] = probe_array;
  return root.dump(2);
}

std::string ObjectComparison::toMarkdown() const {
  std::ostringstream out;
  out << "# GBuffer object acceptance\n\n";
  out << "- Object: `" << object_id << "`\n";
  out << "- Verdict: **" << (identical ? "IDENTICAL" : "CHANGED") << "**\n";
  out << "- Generated: `" << generated_at << "`\n";
  out << "- Original material SHA-256: `" << original_sha256 << "`\n\n";
  out << "## Anchored evidence\n\n";
  out << "| Side | Anchor | Capture | View UTC | Unix ms | Raw file | Raw SHA-256 |\n";
  out << "|---|---|---|---|---:|---|---|\n";
  out << "| left | `" << left_anchor << "` | `" << left_capture_id << "` | `"
      << left_captured_at.utc << "` | " << left_captured_at.unix_ms << " | `"
      << left_raw_file << "` | `" << left_raw_sha256 << "` |\n";
  out << "| right | `" << right_anchor << "` | `" << right_capture_id
      << "` | `" << right_captured_at.utc << "` | "
      << right_captured_at.unix_ms << " | `" << right_raw_file << "` | `"
      << right_raw_sha256 << "` |\n\n";
  out << "## Raw offsets\n\n";
  out << "Source object `" << object_id << "`: `originals/scene.json` offset `"
      << source_range.offset << "`, length `" << source_range.length << "`.\n\n";
  if (corrected) {
    out << "Correction overlay: `" << overlay_path << "`, SHA-256 `"
        << overlay_sha256 << "`, material offset `"
        << overlay_material_range.offset << "`, length `"
        << overlay_material_range.length << "`.\n\n";
  } else {
    out << "Correction overlay: none.\n\n";
  }
  out << "| Attachment | Left offset | Right offset | Bytes | Changed px | "
         "Mean abs | Max abs |\n";
  out << "|---|---:|---:|---:|---:|---:|---:|\n";
  for (const AttachmentMetric& metric : metrics) {
    out << "| `" << metric.attachment << "` | " << metric.left_offset << " | "
        << metric.right_offset << " | " << metric.byte_length << " | "
        << metric.changed_pixels << " | " << std::fixed
        << std::setprecision(6) << metric.mean_absolute_difference << " | "
        << metric.maximum_difference << " |\n";
  }
  out << "\nChanged albedo bounds: ";
  if (changed_bounds.visible) {
    out << "`x=" << changed_bounds.x << ", y=" << changed_bounds.y
        << ", w=" << changed_bounds.width << ", h=" << changed_bounds.height
        << "` (" << changed_bounds.pixel_count << " pixels).\n\n";
  } else {
    out << "none.\n\n";
  }
  out << "## Raw pixel probes\n\n";
  out << "| Probe | XY | Left offset | Right offset | Left bytes | Right bytes | "
         "Changed |\n";
  out << "|---|---:|---:|---:|---|---|---|\n";
  for (const ProbeDelta& probe : probes) {
    out << "| `" << probe.label << "` | " << probe.x << "," << probe.y << " | "
        << probe.left_offset << " | " << probe.right_offset << " | `"
        << probe.left_hex << "` | `" << probe.right_hex << "` | "
        << (probe.changed ? "yes" : "no") << " |\n";
  }
  out << "\n## Acceptance rule\n\n";
  out << "This record is accepted only when the original material hash, both "
         "capture manifests, both raw view hashes, and every offset in the "
         "tables above still match the exported `SHA256SUMS`.\n";
  return out.str();
}

namespace {

void copyVerifiedRaw(const fs::path& source_dir, const ViewEvidence& view,
                     const fs::path& destination_root) {
  const Bytes raw = readViewRaw(source_dir, view);
  writeFile(destination_root / view.raw_file, raw);
}

std::vector<fs::path> recursiveFiles(const fs::path& root) {
  std::vector<fs::path> files;
  std::error_code error;
  for (const fs::directory_entry& entry :
       fs::recursive_directory_iterator(root, error)) {
    if (error) {
      throw std::runtime_error("cannot scan export directory: " +
                               error.message());
    }
    if (entry.is_regular_file()) {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

}  // namespace

ExportResult exportObjectComparison(const Workspace& workspace,
                                    const std::string& object_id,
                                    const std::string& left_anchor,
                                    const std::string& right_anchor,
                                    const fs::path& output_root,
                                    const std::string& left_capture_id,
                                    const std::string& right_capture_id) {
  ObjectComparison comparison =
      compareObject(workspace, object_id, left_anchor, right_anchor,
                    left_capture_id, right_capture_id);
  const auto left_capture =
      workspace.loadCapture(comparison.left_anchor, comparison.left_capture_id);
  const auto right_capture = workspace.loadCapture(
      comparison.right_anchor, comparison.right_capture_id);
  if (!left_capture.has_value() || !right_capture.has_value()) {
    throw std::runtime_error("captures disappeared while exporting");
  }
  const ViewEvidence* left_view =
      requireObjectView(*left_capture, object_id);
  const ViewEvidence* right_view =
      requireObjectView(*right_capture, object_id);
  const fs::path left_dir = workspace.captureDirectory(*left_capture);
  const fs::path right_dir = workspace.captureDirectory(*right_capture);
  const Bytes left_raw = readViewRaw(left_dir, *left_view);
  const Bytes right_raw = readViewRaw(right_dir, *right_view);

  const std::string stamp = sanitizeName(
      nowStamp().utc + "-" + object_id + "-" + comparison.left_anchor +
      "-to-" + comparison.right_anchor);
  const fs::path output =
      (output_root.empty() ? workspace.exportsDir() / ("accept-" + stamp)
                           : output_root)
          .lexically_normal();
  std::error_code error;
  if (fs::exists(output, error)) {
    throw std::runtime_error("export destination already exists: " +
                             output.string());
  }
  ensureDir(output);
  ensureDir(output / "raw" / "left");
  ensureDir(output / "raw" / "right");
  ensureDir(output / "views" / "left");
  ensureDir(output / "views" / "right");
  ensureDir(output / "diff");
  ensureDir(output / "scenes" / "left");
  ensureDir(output / "scenes" / "right");
  ensureDir(output / "scenes" / "diff");
  ensureDir(output / "evidence");
  ensureDir(output / "originals");
  ensureDir(output / "overlays");

  copyVerifiedRaw(left_dir, *left_view, output / "raw" / "left");
  copyVerifiedRaw(right_dir, *right_view, output / "raw" / "right");
  const ViewEvidence* left_scene = left_capture->findView("scene");
  const ViewEvidence* right_scene = right_capture->findView("scene");
  if (left_scene != nullptr) {
    copyVerifiedRaw(left_dir, *left_scene, output / "scenes" / "left");
  }
  if (right_scene != nullptr) {
    copyVerifiedRaw(right_dir, *right_scene, output / "scenes" / "right");
  }
  fs::copy_file(left_dir / "capture.json",
                output / "evidence" / "left-capture.json");
  fs::copy_file(right_dir / "capture.json",
                output / "evidence" / "right-capture.json");
  fs::copy_file(left_dir / "SHA256SUMS",
                output / "evidence" / "left-SHA256SUMS");
  fs::copy_file(right_dir / "SHA256SUMS",
                output / "evidence" / "right-SHA256SUMS");

  for (const AttachmentEvidence& attachment : left_view->attachments) {
    const AttachmentEvidence* right_attachment =
        findAttachment(*right_view, attachment.name);
    if (right_attachment == nullptr) {
      continue;
    }
    const std::string stem = sanitizeName(attachment.name);
    writePpm(output / "views" / "left" / (stem + ".ppm"), *left_view,
             attachment, left_raw, attachment.name);
    writePpm(output / "views" / "right" / (stem + ".ppm"), *right_view,
             *right_attachment, right_raw, attachment.name);
    if (attachment.name == "albedo" || attachment.name == "final") {
      writeDiffPpm(output / "diff" / (stem + ".ppm"), *left_view, attachment,
                   left_raw, *right_view, *right_attachment, right_raw);
    }
  }
  if (left_scene != nullptr && right_scene != nullptr) {
    std::ostringstream scene_evidence;
    scene_evidence << "# Full-scene anchor evidence\n\n";
    scene_evidence << "| Side | UTC | Unix ms | Raw file | Raw SHA-256 |\n";
    scene_evidence << "|---|---|---:|---|---|\n";
    scene_evidence << "| left | `" << left_scene->rendered_at.utc << "` | "
                   << left_scene->rendered_at.unix_ms << " | `"
                   << left_scene->raw_file << "` | `"
                   << left_scene->raw_sha256 << "` |\n";
    scene_evidence << "| right | `" << right_scene->rendered_at.utc << "` | "
                   << right_scene->rendered_at.unix_ms << " | `"
                   << right_scene->raw_file << "` | `"
                   << right_scene->raw_sha256 << "` |\n\n";
    scene_evidence << "| Attachment | Left offset | Right offset | Bytes |\n";
    scene_evidence << "|---|---:|---:|---:|\n";
    for (const AttachmentEvidence& attachment : left_scene->attachments) {
      const AttachmentEvidence* other =
          findAttachment(*right_scene, attachment.name);
      scene_evidence << "| `" << attachment.name << "` | " << attachment.offset
                     << " | " << (other == nullptr ? 0 : other->offset)
                     << " | " << attachment.length << " |\n";
    }
    atomicWriteText(output / "scene-evidence.md", scene_evidence.str());
    const Bytes left_scene_raw = readViewRaw(left_dir, *left_scene);
    const Bytes right_scene_raw = readViewRaw(right_dir, *right_scene);
    for (const AttachmentEvidence& attachment : left_scene->attachments) {
      const AttachmentEvidence* other =
          findAttachment(*right_scene, attachment.name);
      if (other == nullptr) {
        continue;
      }
      const std::string stem = sanitizeName(attachment.name);
      writePpm(output / "scenes" / "left" / (stem + ".ppm"), *left_scene,
               attachment, left_scene_raw, attachment.name);
      writePpm(output / "scenes" / "right" / (stem + ".ppm"), *right_scene,
               *other, right_scene_raw, attachment.name);
      if (attachment.name == "albedo" || attachment.name == "final") {
        writeDiffPpm(output / "scenes" / "diff" / (stem + ".ppm"),
                     *left_scene, attachment, left_scene_raw, *right_scene,
                     *other, right_scene_raw);
      }
    }
  }

  const Bytes original = readFile(workspace.originalScenePath());
  writeFile(output / "originals" / "scene.json", original);
  makeReadOnly(output / "originals" / "scene.json");
  fs::copy_file(workspace.originalManifestPath(),
                output / "evidence" / "original.json");
  fs::copy_file(workspace.journalPath(),
                output / "evidence" / "journal.jsonl");
  if (comparison.corrected && !comparison.overlay_path.empty()) {
    const fs::path overlay_source =
        fs::path(comparison.overlay_path).is_absolute()
            ? fs::path(comparison.overlay_path)
            : workspace.root() / comparison.overlay_path;
    if (fs::is_regular_file(overlay_source)) {
      const fs::path overlay_relative =
          fs::path("overlays") /
          (sanitizeName(comparison.object_id) + ".json");
      fs::copy_file(overlay_source, output / overlay_relative);
      comparison.overlay_path = overlay_relative.generic_string();
    }
  }

  const fs::path comparison_file = output / "comparison.json";
  const fs::path acceptance_file = output / "acceptance.md";
  atomicWriteText(comparison_file, comparison.toJson());
  atomicWriteText(acceptance_file, comparison.toMarkdown());

  std::ostringstream manifest;
  manifest << "{\n";
  manifest << "  \"schema\": \"gbi.export/1\",\n";
  manifest << "  \"comparison_id\": \"" << comparison.comparison_id
           << "\",\n";
  manifest << "  \"object_id\": \"" << object_id << "\",\n";
  manifest << "  \"left_capture_id\": \"" << comparison.left_capture_id
           << "\",\n";
  manifest << "  \"right_capture_id\": \"" << comparison.right_capture_id
           << "\",\n";
  manifest << "  \"generated_at\": \"" << comparison.generated_at << "\"\n";
  manifest << "}\n";
  atomicWriteText(output / "manifest.json", manifest.str());

  std::ostringstream sums;
  for (const fs::path& file : recursiveFiles(output)) {
    if (file == output / "SHA256SUMS") {
      continue;
    }
    sums << sha256File(file) << "  "
         << fs::relative(file, output).generic_string() << '\n';
  }
  atomicWriteText(output / "SHA256SUMS", sums.str());
  return {output, acceptance_file, comparison_file, comparison};
}

}  // namespace gbi
