#pragma once

#include <string>
#include <vector>

#include "png.h"
#include "snapshot.h"

namespace tar {

struct RegionDiff {
  int obj = 0;
  int page = 0;
  int layer = 0;
  int placeholder = 0;
  uint64_t diff_pixels = 0;
  uint32_t max_delta = 0;
  double mean_delta = 0.0;
};

struct FrameDiff {
  int frame = 0;
  uint64_t diff_pixels = 0;
  double diff_percent = 0.0;
  std::string diff_image;  // heatmap PNG, relative to evidence dir
  std::vector<RegionDiff> regions;
};

struct DiffReport {
  std::string label_a;
  std::string label_b;
  int format_a = 0;  // snapshot format versions actually parsed
  int format_b = 0;
  std::vector<FrameDiff> frames;
  uint64_t total_diff_pixels = 0;
  std::vector<std::string> warnings;  // parse + per-frame processing issues
};

// Pixel diff of two images: returns a red-heatmap visualization and
// fills diff_pixels/max_delta. Throws on size mismatch.
Image DiffImages(const Image& a, const Image& b, uint64_t* diff_pixels,
                 uint32_t* max_delta);

// Correlates framebuffer differences with draw calls, page events and
// resource states from both snapshots. Per-frame and per-object work is
// error-isolated: failures land in `warnings` and processing continues.
DiffReport Correlate(const Snapshot& a, const Snapshot& b,
                     const std::string& evidence_dir, Log& log);

}  // namespace tar
