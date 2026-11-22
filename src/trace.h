#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Reflection-probe capture format ("RPRB").
//   v1 (legacy): draw-call records only, no CRC, no framebuffer hashes.
//   v2 (current): draw + resource-state records, per-record CRC32,
//                 per-draw framebuffer hash, footer with totals.
// A v2 stream may be truncated; recovery salvages every complete record
// whose CRC validates and rebuilds a well-formed file with a new footer.

namespace trace {

constexpr uint32_t kMagic = 0x52505242;      // "RPRB"
constexpr uint32_t kFooterMagic = 0x52505246; // "RPRF"
constexpr uint32_t kVersionLegacy = 1;
constexpr uint32_t kVersionCurrent = 2;

enum class RecordType : uint32_t { kDraw = 1, kResourceState = 2 };

struct DrawRecord {
  uint32_t index = 0;        // monotonic step index across the capture
  std::string name;          // e.g. "main/floor-reflect"
  uint32_t pipelineId = 0;
  uint32_t vertexCount = 0;
  uint32_t fbTarget = 0;     // 0 = reflection probe, 1 = main framebuffer
  uint64_t stateHash = 0;    // hash of bound pipeline/resources/push constants
  uint64_t fbHashAfter = 0;  // v2 only: hash of target framebuffer after draw
};

struct ResourceStateRecord {
  uint32_t index = 0;
  std::string name;          // resource name, e.g. "reflectionTex"
  uint32_t resourceId = 0;
  uint32_t oldState = 0;     // VkImageLayout for images
  uint32_t newState = 0;
};

struct Record {
  RecordType type;
  DrawRecord draw;
  ResourceStateRecord res;
};

struct Trace {
  uint32_t version = kVersionCurrent;
  std::vector<Record> records;
  uint64_t finalFbHash = 0;  // v2 footer
  // Recovery metadata (not serialized into a clean file):
  bool recovered = false;
  bool truncated = false;
  uint32_t truncatedAtIndex = 0;  // step index of the first lost record
  size_t droppedBytes = 0;
};

uint32_t crc32(const void* data, size_t len, uint32_t seed = 0);
uint64_t fnv1a64(const void* data, size_t len);

void writeTrace(const std::string& path, const Trace& t);

// strict=true  -> throws on any malformed/truncated input.
// strict=false -> recovery mode: salvages valid records, fills metadata.
Trace readTrace(const std::string& path, bool strict);

// Copies `keepBytes` of `src` into `dst` to simulate a truncated capture.
void truncateFile(const std::string& src, const std::string& dst, size_t keepBytes);

// Writes header + the first `keepRecords` records + `partialBytes` of the
// next record, with no footer: a capture cut off mid-record, as a crashed
// or killed probe process would leave behind.
void writeTruncated(const std::string& path, const Trace& full,
                    size_t keepRecords, size_t partialBytes);

std::string layoutName(uint32_t layout);

}  // namespace trace
