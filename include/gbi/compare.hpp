#pragma once

#include "gbi/evidence.hpp"
#include "gbi/workspace.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gbi {

struct AttachmentMetric {
  std::string attachment;
  std::uint64_t left_offset = 0;
  std::uint64_t right_offset = 0;
  std::uint64_t byte_length = 0;
  std::uint64_t changed_bytes = 0;
  std::uint64_t changed_pixels = 0;
  std::uint64_t total_pixels = 0;
  double mean_absolute_difference = 0.0;
  std::uint32_t maximum_difference = 0;
  std::string left_sha256;
  std::string right_sha256;
};

struct ProbeDelta {
  std::string label;
  std::string attachment;
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint64_t left_offset = 0;
  std::uint64_t right_offset = 0;
  std::string left_hex;
  std::string right_hex;
  bool changed = false;
};

struct ObjectComparison {
  std::string schema = "gbi.comparison/1";
  std::string comparison_id;
  std::string object_id;
  std::string left_capture_id;
  std::string right_capture_id;
  std::string left_anchor;
  std::string right_anchor;
  std::string left_view_id;
  std::string right_view_id;
  TimeStamp left_captured_at;
  TimeStamp right_captured_at;
  std::string left_raw_file;
  std::string right_raw_file;
  std::uint64_t left_raw_size = 0;
  std::uint64_t right_raw_size = 0;
  std::string left_raw_sha256;
  std::string right_raw_sha256;
  SourceRange source_range;
  bool corrected = false;
  std::string overlay_path;
  std::string overlay_sha256;
  SourceRange overlay_material_range;
  BoundingBox changed_bounds;
  bool identical = false;
  std::vector<AttachmentMetric> metrics;
  std::vector<ProbeDelta> probes;
  std::string original_sha256;
  std::string generated_at;

  std::string toJson() const;
  std::string toMarkdown() const;
};

ObjectComparison compareObject(const Workspace& workspace,
                               const std::string& object_id,
                               const std::string& left_anchor,
                               const std::string& right_anchor,
                               const std::string& left_capture_id = {},
                               const std::string& right_capture_id = {});

struct ExportResult {
  fs::path directory;
  fs::path acceptance_file;
  fs::path comparison_file;
  ObjectComparison comparison;
};

ExportResult exportObjectComparison(const Workspace& workspace,
                                    const std::string& object_id,
                                    const std::string& left_anchor,
                                    const std::string& right_anchor,
                                    const fs::path& output_root = {},
                                    const std::string& left_capture_id = {},
                                    const std::string& right_capture_id = {});

Bytes readViewRaw(const fs::path& capture_dir, const ViewEvidence& view);
void writePpm(const fs::path& path, const ViewEvidence& view,
              const AttachmentEvidence& attachment, const Bytes& raw,
              const std::string& mode = {});
void writeDiffPpm(const fs::path& path, const ViewEvidence& left_view,
                  const AttachmentEvidence& left_attachment,
                  const Bytes& left_raw, const ViewEvidence& right_view,
                  const AttachmentEvidence& right_attachment,
                  const Bytes& right_raw);

}  // namespace gbi
