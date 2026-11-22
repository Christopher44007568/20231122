#include "review.h"

#include <cstdio>

#include "paging.h"
#include "snapshot.h"
#include "vk_context.h"

namespace tar {
namespace {

// Removes the temporary staging directory when the scope exits, even on
// exceptions. Evidence under out_dir/evidence is preserved.
struct TempDirGuard {
  std::string path;
  Log& log;
  ~TempDirGuard() {
    if (path.empty() || !FileExists(path)) return;
    RemoveTree(path);
    log.Info("cleanup: removed temp dir " + path);
  }
};

std::string JsonEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      default: out += c;
    }
  }
  return out;
}

void WriteReportJson(const std::string& path, const DiffReport& rep,
                     const Snapshot& b) {
  std::string j = "{\n";
  j += "  \"run_a\": {\"label\": \"" + JsonEscape(rep.label_a) +
       "\", \"snapshot_format\": " + std::to_string(rep.format_a) + "},\n";
  j += "  \"run_b\": {\"label\": \"" + JsonEscape(rep.label_b) +
       "\", \"snapshot_format\": " + std::to_string(rep.format_b) +
       ", \"extension\": \"" + JsonEscape(b.extension) +
       "\", \"extension_enabled\": " + std::to_string(b.extension_enabled) +
       "},\n";
  j += "  \"total_diff_pixels\": " + std::to_string(rep.total_diff_pixels) +
       ",\n  \"frames\": [\n";
  for (size_t fi = 0; fi < rep.frames.size(); ++fi) {
    const FrameDiff& f = rep.frames[fi];
    j += "    {\"frame\": " + std::to_string(f.frame) +
         ", \"diff_pixels\": " + std::to_string(f.diff_pixels) +
         ", \"diff_percent\": " + std::to_string(f.diff_percent) +
         ", \"diff_image\": \"" + JsonEscape(f.diff_image) +
         "\", \"regions\": [\n";
    for (size_t ri = 0; ri < f.regions.size(); ++ri) {
      const RegionDiff& r = f.regions[ri];
      j += "      {\"obj\": " + std::to_string(r.obj) +
           ", \"page\": " + std::to_string(r.page) +
           ", \"layer\": " + std::to_string(r.layer) +
           ", \"placeholder\": " + std::to_string(r.placeholder) +
           ", \"diff_pixels\": " + std::to_string(r.diff_pixels) +
           ", \"max_delta\": " + std::to_string(r.max_delta) +
           ", \"mean_delta\": " + std::to_string(r.mean_delta) + "}";
      j += (ri + 1 < f.regions.size()) ? ",\n" : "\n";
    }
    j += "    ]}";
    j += (fi + 1 < rep.frames.size()) ? ",\n" : "\n";
  }
  j += "  ],\n  \"events_b\": [\n";
  for (size_t i = 0; i < b.events.size(); ++i) {
    const PageEvent& e = b.events[i];
    j += "    {\"frame\": " + std::to_string(e.frame) + ", \"kind\": \"" +
         JsonEscape(e.kind) + "\", \"page\": " + std::to_string(e.page) +
         ", \"obj\": " + std::to_string(e.obj) + ", \"msg\": \"" +
         JsonEscape(e.message) + "\"}";
    j += (i + 1 < b.events.size()) ? ",\n" : "\n";
  }
  j += "  ],\n  \"warnings\": [\n";
  for (size_t i = 0; i < rep.warnings.size(); ++i) {
    j += "    \"" + JsonEscape(rep.warnings[i]) + "\"";
    j += (i + 1 < rep.warnings.size()) ? ",\n" : "\n";
  }
  j += "  ]\n}\n";
  WriteFile(path, j.data(), j.size());
}

void WriteReportMd(const std::string& path, const DiffReport& rep,
                   const Snapshot& a, const Snapshot& b) {
  std::string m;
  m += "# Texture Array Paging Review\n\n";
  m += "- baseline run: `" + rep.label_a + "` (snapshot format TAPS " +
       std::to_string(rep.format_a) + ")\n";
  m += "- extension run: `" + rep.label_b + "` (snapshot format TAPS " +
       std::to_string(rep.format_b) + ")\n";
  m += "- extension under review: `" +
       (b.extension.empty() ? std::string("-") : b.extension) + "` (enabled=" +
       std::to_string(b.extension_enabled) + ")\n";
  m += "- total differing pixels: " + std::to_string(rep.total_diff_pixels) +
       "\n\n";
  m += "## Per-frame correlation\n\n";
  m += "| frame | diff px | diff % | faulting objects (page) | diff heatmap |\n";
  m += "|---|---|---|---|---|\n";
  for (const auto& f : rep.frames) {
    std::string faults;
    for (const auto& e : b.events) {
      if (e.frame == f.frame && e.kind == "pagefault") {
        if (!faults.empty()) faults += ", ";
        faults += std::to_string(e.obj) + "(p" + std::to_string(e.page) + ")";
      }
    }
    if (faults.empty()) faults = "-";
    m += "| " + std::to_string(f.frame) + " | " +
         std::to_string(f.diff_pixels) + " | " +
         std::to_string(f.diff_percent).substr(0, 5) + " | " + faults + " | `" +
         f.diff_image + "` |\n";
  }
  m += "\n## Objects with framebuffer differences (extension run)\n\n";
  m += "| frame | obj | page | layer | placeholder | diff px | max delta |\n";
  m += "|---|---|---|---|---|---|---|\n";
  for (const auto& f : rep.frames) {
    for (const auto& r : f.regions) {
      if (r.diff_pixels == 0) continue;
      m += "| " + std::to_string(f.frame) + " | " + std::to_string(r.obj) +
           " | " + std::to_string(r.page) + " | " + std::to_string(r.layer) +
           " | " + std::to_string(r.placeholder) + " | " +
           std::to_string(r.diff_pixels) + " | " + std::to_string(r.max_delta) +
           " |\n";
    }
  }
  m += "\n## Errors and warnings (processing continued past these)\n\n";
  bool any = false;
  for (const auto& e : b.events) {
    if (e.kind != "error") continue;
    any = true;
    m += "- frame " + std::to_string(e.frame) + " obj " + std::to_string(e.obj) +
         ": " + e.message + "\n";
  }
  for (const auto& w : rep.warnings) m += "- " + w + "\n";
  if (!any && rep.warnings.empty()) m += "- none\n";
  m += "\n## Resource states (extension run)\n\n";
  for (const auto& rs : b.resources) {
    m += "- frame " + std::to_string(rs.frame) + ": resident=[";
    for (size_t i = 0; i < rs.resident.size(); ++i) {
      if (i) m += ",";
      m += std::to_string(rs.resident[i]);
    }
    m += "] evicted=[";
    for (size_t i = 0; i < rs.evicted.size(); ++i) {
      if (i) m += ",";
      m += std::to_string(rs.evicted[i]);
    }
    m += "] membytes=" + std::to_string(rs.membytes) + "\n";
  }
  WriteFile(path, m.data(), m.size());
  (void)a;
}

}  // namespace

Snapshot RunCapture(const RenderConfig& cfg, int snapshot_format,
                    const std::string& snapshot_path, Log& log) {
  VkContext ctx(log);
  Snapshot snap;
  {
    Renderer renderer(ctx, cfg, log);
    snap = renderer.Run();
  }
  WriteSnapshot(snapshot_path, snap, snapshot_format);
  log.Info("wrote snapshot " + snapshot_path + " (TAPS " +
           std::to_string(snapshot_format) + ")");
  return snap;
}

int RunReview(const ReviewOptions& opt, Log& log) {
  const std::string evidence = JoinPath(opt.out_dir, "evidence");
  MakeDirs(evidence);
  TempDirGuard tmp{JoinPath(opt.out_dir, "tmp"), log};
  MakeDirs(tmp.path);

  RenderConfig base;
  base.frames = opt.frames;
  base.grid = opt.grid;
  base.shader_dir = opt.shader_dir;
  base.evidence_dir = evidence;
  base.label = "v1";
  base.paging.extension_enabled = false;

  RenderConfig ext = base;
  ext.label = "v2";
  ext.paging.extension_enabled = true;
  ext.inject_fault_objs = opt.inject_fault_objs;

  // Two version snapshots around the unknown extension: legacy TAPS 1 for
  // the baseline, current TAPS 2 for the extension run.
  RunCapture(base, 1, JoinPath(evidence, "snapshot_v1.taps"), log);
  RunCapture(ext, 2, JoinPath(evidence, "snapshot_v2.taps"), log);

  // Re-parse through the compat reader: both formats land in one model.
  Snapshot a = ReadSnapshot(JoinPath(evidence, "snapshot_v1.taps"));
  Snapshot b = ReadSnapshot(JoinPath(evidence, "snapshot_v2.taps"));
  log.Info("parsed snapshots: v1 format=" + std::to_string(a.format_version) +
           " v2 format=" + std::to_string(b.format_version));

  DiffReport rep = Correlate(a, b, evidence, log);
  WriteReportJson(JoinPath(evidence, "report.json"), rep, b);
  WriteReportMd(JoinPath(evidence, "report.md"), rep, a, b);
  log.Info("wrote report " + JoinPath(evidence, "report.md"));
  log.Info("review complete: total diff pixels = " +
           std::to_string(rep.total_diff_pixels));
  return 0;
}

}  // namespace tar
