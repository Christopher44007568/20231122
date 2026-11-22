#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "util.h"

namespace tar {

// In-memory, version-neutral snapshot model. Both the legacy TAPS 1
// format and the current TAPS 2 format parse into this structure.
struct DrawCall {
  int frame = 0;
  int obj = 0;
  int layer = 0;
  int page = 0;
  int set = 0;
  int binding = 0;
  int placeholder = 0;  // v2: 1 when the draw used a fallback page
  int x = 0, y = 0, w = 0, h = 0;
};

struct ResourceState {
  int frame = 0;
  std::vector<int> resident;
  std::vector<int> evicted;   // v2 only; empty for v1
  uint64_t membytes = 0;      // v2 only; 0 for v1
};

struct PageEvent {
  int frame = 0;
  int page = -1;
  int obj = -1;
  std::string kind;  // "pagefault" | "error" (v2 only)
  std::string message;
};

struct FramebufferRef {
  int frame = 0;
  std::string file;
  uint32_t crc = 0;
};

struct Snapshot {
  int format_version = 0;  // 1 = legacy, 2 = current
  std::string label;       // e.g. "v1" / "v2" run tag
  std::string extension;   // emulated extension name (v2), "" for v1
  int extension_enabled = 0;
  int frames = 0;
  int width = 0;
  int height = 0;
  int objects = 0;
  std::vector<DrawCall> draws;
  std::vector<ResourceState> resources;
  std::vector<PageEvent> events;
  std::vector<FramebufferRef> framebuffers;
  std::vector<std::string> warnings;  // parse-time compat notes
};

// Writes `snap` in the requested on-disk format (1 or 2).
void WriteSnapshot(const std::string& path, const Snapshot& snap, int format_version);

// Reads either format; dispatches on the "TAPS <n>" header. Unknown keys
// produce warnings (forward compatibility), malformed lines produce
// warnings and are skipped (error resilience).
Snapshot ReadSnapshot(const std::string& path);

}  // namespace tar
