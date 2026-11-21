#include "trace.h"

#include <cstdio>
#include <cstring>

namespace rpsc {

const char* opName(OpType t) {
  switch (t) {
    case OpType::CreatePipeline: return "CreatePipeline";
    case OpType::DestroyPipeline: return "DestroyPipeline";
    case OpType::BindPipeline: return "BindPipeline";
    case OpType::BeginFrame: return "BeginFrame";
    case OpType::EndFrame: return "EndFrame";
    case OpType::Draw: return "Draw";
    case OpType::Barrier: return "Barrier";
    case OpType::Checkpoint: return "Checkpoint";
  }
  return "?";
}

uint32_t crc32(const void* data, size_t size, uint32_t seed) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    init = true;
  }
  uint32_t c = ~seed;
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return ~c;
}

uint64_t fnv1a64(const void* data, size_t size, uint64_t seed) {
  uint64_t h = seed;
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

static void putU32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back(uint8_t(v));
  b.push_back(uint8_t(v >> 8));
  b.push_back(uint8_t(v >> 16));
  b.push_back(uint8_t(v >> 24));
}

static uint32_t getU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}

static std::vector<uint8_t> serialize(const Trace& trace) {
  std::vector<uint8_t> b;
  putU32(b, kMagic);
  putU32(b, trace.version);
  putU32(b, trace.version == kVersionCurrent ? uint32_t(trace.records.size())
                                             : kUnknownCount);
  uint32_t hdrCrc = trace.version == kVersionCurrent ? crc32(b.data(), b.size()) : 0;
  putU32(b, hdrCrc);
  for (const Record& r : trace.records) {
    if (trace.version == kVersionLegacy) {
      b.push_back(uint8_t(r.type));
      b.push_back(0);
      b.push_back(0);
      b.push_back(0);
      for (uint32_t word : r.w) putU32(b, word);
    } else {
      size_t recStart = b.size();
      b.push_back(uint8_t(r.type));
      b.push_back(0);  // flags
      b.push_back(32);  // payload len (u16, little-endian)
      b.push_back(0);
      for (uint32_t word : r.w) putU32(b, word);
      uint32_t crc = crc32(b.data() + recStart, b.size() - recStart);
      putU32(b, crc);
    }
  }
  return b;
}

bool writeTrace(const std::string& path, const Trace& trace, std::string* err) {
  std::vector<uint8_t> bytes = serialize(trace);
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) {
    *err = "cannot open for write: " + path;
    return false;
  }
  bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
  std::fclose(f);
  if (!ok) *err = "short write: " + path;
  return ok;
}

bool walkRecords(const std::vector<uint8_t>& bytes, uint32_t* versionOut,
                 Defect* defect,
                 const std::function<bool(const Record&)>& onRecord,
                 std::string* err) {
  *defect = Defect{};
  if (bytes.size() < kHeaderSize) {
    defect->found = true;
    defect->reason = "TruncatedHeader(file header)";
    defect->bytesPresent = bytes.size();
    defect->bytesExpected = kHeaderSize;
    return false;
  }
  uint32_t magic = getU32(bytes.data());
  uint32_t version = getU32(bytes.data() + 4);
  if (magic != kMagic) {
    *err = "bad magic";
    defect->found = true;
    defect->reason = "BadMagic";
    return false;
  }
  if (version != kVersionLegacy && version != kVersionCurrent) {
    *err = "unsupported version " + std::to_string(version);
    defect->found = true;
    defect->reason = "BadVersion";
    return false;
  }
  *versionOut = version;
  if (version == kVersionCurrent) {
    uint32_t want = crc32(bytes.data(), 12);
    if (want != getU32(bytes.data() + 12)) {
      *err = "header crc mismatch";
      defect->found = true;
      defect->reason = "HeaderCrcMismatch";
      return false;
    }
  }
  size_t pos = kHeaderSize;
  size_t index = 0;
  while (pos < bytes.size()) {
    Record r;
    if (version == kVersionLegacy) {
      if (bytes.size() - pos < kV1RecordSize) {
        defect->found = true;
        defect->offset = pos;
        defect->recordIndex = index;
        defect->bytesPresent = bytes.size() - pos;
        defect->bytesExpected = kV1RecordSize;
        defect->reason = "TruncatedPayload";
        if (defect->bytesPresent >= 1) defect->partialType = bytes[pos];
        return false;
      }
      r.type = OpType(bytes[pos]);
      for (int i = 0; i < 8; ++i) r.w[i] = getU32(bytes.data() + pos + 4 + i * 4);
      pos += kV1RecordSize;
    } else {
      if (bytes.size() - pos < kV2HeaderSize) {
        defect->found = true;
        defect->offset = pos;
        defect->recordIndex = index;
        defect->bytesPresent = bytes.size() - pos;
        defect->bytesExpected = kV2HeaderSize;
        defect->reason = "TruncatedHeader";
        if (defect->bytesPresent >= 1) defect->partialType = bytes[pos];
        return false;
      }
      uint8_t type = bytes[pos];
      uint16_t len = uint16_t(bytes[pos + 2]) | (uint16_t(bytes[pos + 3]) << 8);
      size_t total = kV2HeaderSize + len + 4;
      if (bytes.size() - pos < total) {
        defect->found = true;
        defect->offset = pos;
        defect->recordIndex = index;
        defect->partialType = type;
        defect->bytesPresent = bytes.size() - pos;
        defect->bytesExpected = total;
        defect->reason = "TruncatedPayload";
        return false;
      }
      uint32_t wantCrc = getU32(bytes.data() + pos + kV2HeaderSize + len);
      uint32_t gotCrc = crc32(bytes.data() + pos, kV2HeaderSize + len);
      if (wantCrc != gotCrc) {
        defect->found = true;
        defect->offset = pos;
        defect->recordIndex = index;
        defect->partialType = type;
        defect->bytesPresent = total;
        defect->bytesExpected = total;
        defect->reason = "CrcMismatch";
        return false;
      }
      r.type = OpType(type);
      size_t words = len / 4 < 8 ? len / 4 : 8;
      for (size_t i = 0; i < words; ++i)
        r.w[i] = getU32(bytes.data() + pos + kV2HeaderSize + i * 4);
      pos += total;
    }
    if (!onRecord(r)) return true;  // consumer asked to stop; not a defect
    ++index;
  }
  return true;
}


bool readTraceStrict(const std::string& path, Trace* out, std::string* err) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    *err = "cannot open: " + path;
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> bytes(static_cast<size_t>(n));
  bool ok = std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
  std::fclose(f);
  if (!ok) {
    *err = "short read: " + path;
    return false;
  }
  Defect defect;
  out->records.clear();
  bool clean = walkRecords(bytes, &out->version, &defect,
                           [&](const Record& r) {
                             out->records.push_back(r);
                             return true;
                           },
                           err);
  if (!clean && err->empty()) {
    *err = "defect at offset " + std::to_string(defect.offset) + ": " +
           defect.reason;
  }
  return clean;
}

Trace convertV1toV2(const Trace& legacy) {
  Trace t;
  t.version = kVersionCurrent;
  t.records = legacy.records;
  return t;
}

}  // namespace rpsc
