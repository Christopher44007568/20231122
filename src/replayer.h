#pragma once

#include <cstdint>
#include <map>
#include <string>

struct ReplayOptions {
  std::string capturePath;
  std::string evidenceRoot = "evidence";
  std::map<uint32_t, uint32_t> topologyOverrides;  // draw index -> topology
  uint8_t tolerance = 0;                           // per-channel diff tolerance
};

// Replays a recovered capture, correlates draw calls / resource states /
// framebuffer / image differences, preserves evidence, and writes a report
// with separated automated conclusions and manual confirmation items.
//
// Exit codes: 0 = no error-level findings (warnings may still be reported),
// 2 = error-level findings (see report), 1 = tool error.
int replayCapture(const ReplayOptions& opts);
