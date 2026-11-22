#include "diff.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "util.h"

namespace tar {

Image DiffImages(const Image& a, const Image& b, uint64_t* diff_pixels,
                 uint32_t* max_delta) {
  if (a.width != b.width || a.height != b.height) {
    throw PipelineError("framebuffer size mismatch: " + std::to_string(a.width) +
                        "x" + std::to_string(a.height) + " vs " +
                        std::to_string(b.width) + "x" + std::to_string(b.height));
  }
  Image heat;
  heat.width = a.width;
  heat.height = a.height;
  heat.rgba.resize(a.rgba.size());
  uint64_t ndiff = 0;
  uint32_t maxd = 0;
  for (size_t i = 0; i < a.rgba.size(); i += 4) {
    uint32_t d = 0;
    for (int c = 0; c < 3; ++c) {
      d = std::max(d, static_cast<uint32_t>(
                          std::abs(static_cast<int>(a.rgba[i + c]) -
                                   static_cast<int>(b.rgba[i + c]))));
    }
    if (d > 0) ++ndiff;
    maxd = std::max(maxd, d);
    heat.rgba[i + 0] = static_cast<uint8_t>(d);
    heat.rgba[i + 1] = static_cast<uint8_t>(d / 4);
    heat.rgba[i + 2] = 0;
    heat.rgba[i + 3] = 255;
  }
  *diff_pixels = ndiff;
  *max_delta = maxd;
  return heat;
}

namespace {

// Accumulates diff statistics over the framebuffer region of one draw call.
void RegionStats(const Image& heat, const DrawCall& d, RegionDiff* out) {
  uint64_t ndiff = 0;
  uint64_t sum = 0;
  uint32_t maxd = 0;
  for (int y = d.y; y < d.y + d.h; ++y) {
    for (int x = d.x; x < d.x + d.w; ++x) {
      size_t i = (static_cast<size_t>(y) * heat.width + x) * 4;
      uint32_t delta = heat.rgba[i];
      if (delta > 0) ++ndiff;
      sum += delta;
      maxd = std::max(maxd, delta);
    }
  }
  const double area = static_cast<double>(d.w) * d.h;
  out->diff_pixels = ndiff;
  out->max_delta = maxd;
  out->mean_delta = area > 0 ? static_cast<double>(sum) / area : 0.0;
}

const FramebufferRef* FindFb(const Snapshot& s, int frame) {
  for (const auto& fb : s.framebuffers) {
    if (fb.frame == frame) return &fb;
  }
  return nullptr;
}

}  // namespace

DiffReport Correlate(const Snapshot& a, const Snapshot& b,
                     const std::string& evidence_dir, Log& log) {
  DiffReport rep;
  rep.label_a = a.label;
  rep.label_b = b.label;
  rep.format_a = a.format_version;
  rep.format_b = b.format_version;
  rep.warnings.insert(rep.warnings.end(), a.warnings.begin(), a.warnings.end());
  rep.warnings.insert(rep.warnings.end(), b.warnings.begin(), b.warnings.end());

  const int frames = std::min(a.frames, b.frames);
  for (int frame = 0; frame < frames; ++frame) {
    // Per-frame error isolation: one bad frame never stops the review.
    try {
      const FramebufferRef* fba = FindFb(a, frame);
      const FramebufferRef* fbb = FindFb(b, frame);
      if (!fba || !fbb) throw PipelineError("missing framebuffer reference");
      Image ia = ReadPng(JoinPath(evidence_dir, fba->file));
      Image ib = ReadPng(JoinPath(evidence_dir, fbb->file));
      if (Crc32(ia.rgba.data(), ia.rgba.size()) != fba->crc) {
        rep.warnings.push_back("crc mismatch on " + fba->file);
      }
      if (Crc32(ib.rgba.data(), ib.rgba.size()) != fbb->crc) {
        rep.warnings.push_back("crc mismatch on " + fbb->file);
      }
      uint64_t ndiff = 0;
      uint32_t maxd = 0;
      Image heat = DiffImages(ia, ib, &ndiff, &maxd);

      FrameDiff fd;
      fd.frame = frame;
      fd.diff_pixels = ndiff;
      fd.diff_percent =
          100.0 * static_cast<double>(ndiff) /
          (static_cast<double>(heat.width) * heat.height);
      char name[64];
      std::snprintf(name, sizeof(name), "diff_%s_%s_%03d.png", a.label.c_str(),
                    b.label.c_str(), frame);
      fd.diff_image = name;
      WritePng(JoinPath(evidence_dir, name), heat);

      // Correlate the heatmap back to each draw call's framebuffer region.
      for (const auto& d : b.draws) {
        if (d.frame != frame) continue;
        try {
          RegionDiff rd;
          rd.obj = d.obj;
          rd.page = d.page;
          rd.layer = d.layer;
          rd.placeholder = d.placeholder;
          RegionStats(heat, d, &rd);
          fd.regions.push_back(rd);
        } catch (const std::exception& e) {
          rep.warnings.push_back("frame " + std::to_string(frame) + " obj " +
                                 std::to_string(d.obj) + ": " + e.what());
        }
      }
      rep.total_diff_pixels += ndiff;
      rep.frames.push_back(std::move(fd));
    } catch (const std::exception& e) {
      log.Warn("frame " + std::to_string(frame) +
               " diff failed: " + e.what() + " (continuing)");
      rep.warnings.push_back("frame " + std::to_string(frame) +
                             " diff failed: " + e.what());
    }
  }
  return rep;
}

}  // namespace tar
