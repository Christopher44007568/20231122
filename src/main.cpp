#include "common.h"
#include "evidence.h"
#include "slice_renderer.h"
#include "voxel_io.h"
#include "vulkan_context.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

namespace vox {
namespace {

struct ManifestEntry {
  std::string object_id;
  std::string legacy_file;
  std::string modern_file;
};

struct DiffStats {
  uint64_t differing_pixels = 0;
  uint64_t total_pixels = 0;
  uint64_t max_channel_delta = 0;
  uint64_t mean_abs_error_milli = 0;
};

std::vector<ManifestEntry> read_manifest(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw SlicerError("cannot open manifest: " + path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) lines.push_back(line);
  }
  if (lines.size() % 3 != 0) {
    throw SlicerError("manifest must contain groups of 3 lines");
  }
  std::vector<ManifestEntry> out;
  for (size_t i = 0; i < lines.size(); i += 3) {
    out.push_back({lines[i], lines[i + 1], lines[i + 2]});
  }
  return out;
}

DiffStats diff_pixels(const std::vector<uint8_t>& a,
                      const std::vector<uint8_t>& b) {
  DiffStats s;
  const size_t count = std::min(a.size(), b.size());
  s.total_pixels = count / 4;
  uint64_t sum_abs = 0;
  for (size_t i = 0; i < count; ++i) {
    int d = std::abs(int(a[i]) - int(b[i]));
    sum_abs += static_cast<uint64_t>(d);
    s.max_channel_delta = std::max(s.max_channel_delta, uint64_t(d));
  }
  for (size_t p = 0; p < s.total_pixels; ++p) {
    if (std::memcmp(a.data() + p * 4, b.data() + p * 4, 4) != 0) {
      ++s.differing_pixels;
    }
  }
  s.mean_abs_error_milli =
      count ? (sum_abs * 1000) / count : 0;
  return s;
}

void mkdir_p(const std::string& path) {
  std::string cur;
  for (char c : path) {
    cur += c;
    if (c == '/') mkdir(cur.c_str(), 0755);
  }
  mkdir(path.c_str(), 0755);
}

} // namespace
} // namespace vox

int main(int argc, char** argv) {
  using namespace vox;
  std::string input_dir = argc > 1 ? argv[1] : "data/input";
  std::string output_dir = argc > 2 ? argv[2] : "evidence_out";
  mkdir_p(output_dir);
  EvidenceLog log(output_dir + "/pipeline.jsonl");
  std::vector<ManifestEntry> entries =
      read_manifest(input_dir + "/manifest.txt");
  log.event("pipeline_start")
      .field("input_dir", input_dir)
      .field("output_dir", output_dir)
      .field("object_count", uint64_t(entries.size()));

  constexpr uint32_t kRenderW = 8, kRenderH = 6;
  VulkanContext ctx(kRenderW, kRenderH);
  log.event("vulkan_ready")
      .field("device", ctx.device_name())
      .field("api_version", uint64_t(ctx.api_version()))
      .field("queue_family", uint64_t(ctx.queue_family()));
  SliceRenderer renderer(ctx, log);

  struct ObjectReport {
    std::string object_id;
    std::string status;
    std::string detail;
    bool truncated = false;
  };
  std::vector<ObjectReport> reports;
  uint32_t truncated_total = 0;

  for (const auto& entry : entries) {
    ObjectReport report{entry.object_id, "ok", "", false};
    try {
      log.event("object_begin").field("object", entry.object_id);
      Snapshot legacy = load_snapshot(
          input_dir + "/" + entry.object_id + "/" + entry.legacy_file,
          entry.object_id);
      Snapshot modern = load_snapshot(
          input_dir + "/" + entry.object_id + "/" + entry.modern_file,
          entry.object_id);
      normalize_layout(legacy);
      normalize_layout(modern);
      if (legacy.truncated || modern.truncated) {
        report.truncated = true;
        ++truncated_total;
      }
      if (legacy.dims.x != modern.dims.x ||
          legacy.dims.y != modern.dims.y ||
          legacy.dims.z != modern.dims.z) {
        throw SlicerError("snapshot dimension mismatch across versions");
      }

      auto old_slices = renderer.render_snapshot(legacy, "legacy_v1");
      auto new_slices = renderer.render_snapshot(modern, "modern_v2");
      if (old_slices.size() != new_slices.size()) {
        throw SlicerError("slice count divergence after rendering");
      }

      std::string obj_dir = output_dir + "/" + entry.object_id;
      mkdir_p(obj_dir + "/legacy_v1");
      mkdir_p(obj_dir + "/modern_v2");
      mkdir_p(obj_dir + "/diff");
      uint64_t object_diff_pixels = 0;
      for (size_t i = 0; i < old_slices.size(); ++i) {
        write_ppm(obj_dir + "/legacy_v1/slice_" + std::to_string(i) + ".ppm",
                  kRenderW, kRenderH, old_slices[i].rgba);
        write_ppm(obj_dir + "/modern_v2/slice_" + std::to_string(i) + ".ppm",
                  kRenderW, kRenderH, new_slices[i].rgba);
        DiffStats d = diff_pixels(old_slices[i].rgba, new_slices[i].rgba);
        object_diff_pixels += d.differing_pixels;
        // Black canvas with magenta markers keeps the review focus on the
        // exact cells where the two versions diverge.
        std::vector<uint8_t> mask(old_slices[i].rgba.size(), 0);
        for (size_t p = 0; p < d.total_pixels; ++p) {
          if (std::memcmp(old_slices[i].rgba.data() + p * 4,
                          new_slices[i].rgba.data() + p * 4, 4) != 0) {
            mask[p * 4] = 255;
            mask[p * 4 + 1] = 0;
            mask[p * 4 + 2] = 255;
            mask[p * 4 + 3] = 255;
          } else {
            mask[p * 4 + 3] = 255;
          }
        }
        write_ppm(obj_dir + "/diff/slice_" + std::to_string(i) + ".ppm",
                  kRenderW, kRenderH, mask);
        log.event("frame_diff")
            .field("object", entry.object_id)
            .field("slice", uint64_t(i))
            .field("differing_pixels", d.differing_pixels)
            .field("total_pixels", d.total_pixels)
            .field("max_channel_delta", d.max_channel_delta)
            .field("mean_abs_error_milli", d.mean_abs_error_milli)
            .field("legacy_fingerprint", old_slices[i].fingerprint)
            .field("modern_fingerprint", new_slices[i].fingerprint);
      }
      log.event("object_end")
          .field("object", entry.object_id)
          .field("status", "ok")
          .field("truncated", report.truncated)
          .field("legacy_expected", legacy.expected_bytes)
          .field("legacy_observed", legacy.observed_bytes)
          .field("modern_expected", modern.expected_bytes)
          .field("modern_observed", modern.observed_bytes)
          .field("object_diff_pixels", object_diff_pixels);
      report.detail =
          report.truncated ? "truncated record handled" : "complete pair";
    } catch (const std::exception& ex) {
      report.status = "error";
      report.detail = ex.what();
      log.event("object_error")
          .field("object", entry.object_id)
          .field("message", ex.what());
      std::cerr << "[warn] object " << entry.object_id << " failed: "
                << ex.what() << " (continuing)\n";
    }
    reports.push_back(report);
  }

  std::ofstream summary(output_dir + "/review_summary.json");
  summary << "{\n  \"truncated_records\": " << truncated_total << ",\n";
  summary << "  \"objects\": [\n";
  for (size_t i = 0; i < reports.size(); ++i) {
    const auto& r = reports[i];
    summary << "    {\"id\":\"" << json_escape(r.object_id)
            << "\",\"status\":\"" << json_escape(r.status)
            << "\",\"truncated\":" << (r.truncated ? "true" : "false")
            << ",\"detail\":\"" << json_escape(r.detail) << "\"}"
            << (i + 1 < reports.size() ? "," : "") << "\n";
  }
  summary << "  ]\n}\n";
  uint64_t error_count = static_cast<uint64_t>(
      std::count_if(reports.begin(), reports.end(),
                    [](const ObjectReport& r) {
                      return r.status == "error";
                    }));
  log.event("pipeline_end")
      .field("objects_processed", uint64_t(reports.size()))
      .field("truncated_records", uint64_t(truncated_total))
      .field("errors", error_count);
  log.flush();
  std::cout << "review complete: " << reports.size() << " objects, "
            << truncated_total << " truncated record(s), summary at "
            << output_dir << "/review_summary.json\n";
  return 0;
}
