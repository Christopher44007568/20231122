#pragma once

#include <cstdint>
#include <string>

struct RecordOptions {
  std::string outPath;
  bool legacyFormat = false;       // write v1 (legacy) capture instead of v2
  int corruptDraw = -1;            // draw index whose topology is corrupted in the file
  uint32_t corruptTopology = 0;    // topology written instead (simulates bad recovery)
};

// Renders the demo scene offscreen and writes a PTDC capture. The capture
// always records exactly one extension unknown to the replayer, so the
// diagnostic flow has a realistic unknown-extension record to preserve.
int recordCapture(const RecordOptions& opts);
