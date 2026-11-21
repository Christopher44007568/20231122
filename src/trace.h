#pragma once
// Render Pipeline State Cache (RPSC) trace format.
//
// Two on-disk versions are supported:
//   v1 (legacy): fixed-size records, no checksums, no checkpoints.
//   v2 (current): length-prefixed records with CRC32, plus Checkpoint
//                 records carrying golden framebuffer/state hashes.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rpsc {

constexpr uint32_t kMagic = 0x43535052;  // 'RPSC' little-endian
constexpr uint32_t kVersionLegacy = 1;
constexpr uint32_t kVersionCurrent = 2;
constexpr uint32_t kUnknownCount = 0xFFFFFFFFu;
constexpr uint32_t kHeaderSize = 16;
constexpr uint32_t kV1RecordSize = 36;  // 4 byte header + 8 x u32 payload
constexpr uint32_t kV2HeaderSize = 4;   // type, flags, len(u16)

enum class OpType : uint8_t {
  CreatePipeline = 1,
  DestroyPipeline = 2,
  BindPipeline = 3,
  BeginFrame = 4,
  EndFrame = 5,
  Draw = 6,
  Barrier = 7,
  Checkpoint = 8,  // v2 only
};

const char* opName(OpType t);

struct PipelineDesc {
  uint32_t id = 0;
  uint32_t blendEnable = 0;
  uint32_t cullMode = 0;        // VkCullModeFlags subset
  uint32_t colorWriteMask = 0;  // VkColorComponentFlags
  uint32_t viewportW = 0;
  uint32_t viewportH = 0;
};

struct Record {
  OpType type = OpType::BeginFrame;
  // Generic 8-word payload; meaning depends on type:
  //  CreatePipeline: id, blend, cull, mask, vpW, vpH, -, -
  //  BindPipeline:   id
  //  Begin/EndFrame: frameIndex
  //  Draw:           vertexCount, firstVertex, instanceCount, rgba(packed)
  //  Barrier:        imageId, oldLayout, newLayout
  //  Checkpoint:     fbHashLo, fbHashHi, stateHashLo, stateHashHi
  uint32_t w[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint64_t u64(int i) const {
    return uint64_t(w[i * 2]) | (uint64_t(w[i * 2 + 1]) << 32);
  }
  void setU64(int i, uint64_t v) {
    w[i * 2] = uint32_t(v);
    w[i * 2 + 1] = uint32_t(v >> 32);
  }
};

struct Trace {
  uint32_t version = kVersionCurrent;
  std::vector<Record> records;
};

uint32_t crc32(const void* data, size_t size, uint32_t seed = 0);
uint64_t fnv1a64(const void* data, size_t size, uint64_t seed = 1469598103934665603ull);

struct Defect {
  bool found = false;
  size_t offset = 0;        // file offset of the defective record
  size_t recordIndex = 0;   // 0-based index the record would have had
  int partialType = -1;     // OpType byte if readable, else -1
  size_t bytesPresent = 0;  // bytes of this record still in the file
  size_t bytesExpected = 0; // bytes a complete record would need
  std::string reason;       // TruncatedHeader | TruncatedPayload | CrcMismatch | BadMagic | ...
};

// Walks a trace image in memory, invoking `onRecord` for every fully valid
// record and stopping at the first defect. Returns true when the stream is
// clean to the end. Shared by the strict reader and the recovery pass.
bool walkRecords(const std::vector<uint8_t>& bytes, uint32_t* versionOut,
                 Defect* defect,
                 const std::function<bool(const Record&)>& onRecord,
                 std::string* err);

// Serialization. Returns false with `err` set on malformed input.
bool writeTrace(const std::string& path, const Trace& trace, std::string* err);
// Strict read: fails on truncation / CRC mismatch (v2).
bool readTraceStrict(const std::string& path, Trace* out, std::string* err);

// Lossless conversion legacy -> current (drops nothing; checkpoints absent).
Trace convertV1toV2(const Trace& legacy);

}  // namespace rpsc
