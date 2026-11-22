#include "trace.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace trace {

uint32_t crc32(const void* data, size_t len, uint32_t seed) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  uint32_t c = ~seed;
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return ~c;
}

uint64_t fnv1a64(const void* data, size_t len) {
  uint64_t h = 1469598103934665603ull;
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}

namespace {

void putU32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF);
  b.push_back((v >> 16) & 0xFF); b.push_back((v >> 24) & 0xFF);
}
void putU64(std::vector<uint8_t>& b, uint64_t v) {
  for (int i = 0; i < 8; ++i) b.push_back((v >> (8 * i)) & 0xFF);
}
void putStr(std::vector<uint8_t>& b, const std::string& s) {
  putU32(b, static_cast<uint32_t>(s.size()));
  b.insert(b.end(), s.begin(), s.end());
}

struct Reader {
  const uint8_t* p; size_t n; size_t off = 0;
  bool u32(uint32_t& v) {
    if (off + 4 > n) return false;
    v = p[off] | (p[off+1] << 8) | (p[off+2] << 16) | (uint32_t(p[off+3]) << 24);
    off += 4; return true;
  }
  bool u64(uint64_t& v) {
    if (off + 8 > n) return false;
    v = 0; for (int i = 0; i < 8; ++i) v |= uint64_t(p[off+i]) << (8*i);
    off += 8; return true;
  }
  bool str(std::string& s) {
    uint32_t len; if (!u32(len) || off + len > n) return false;
    s.assign(reinterpret_cast<const char*>(p + off), len); off += len; return true;
  }
};

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + path);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

}  // namespace

void appendRecordBytes(std::vector<uint8_t>& b, uint32_t version,
                       const Record& r) {
  if (r.type == RecordType::kDraw) {
    std::vector<uint8_t> rec;
    putU32(rec, static_cast<uint32_t>(RecordType::kDraw));
    putU32(rec, r.draw.index);
    putStr(rec, r.draw.name);
    putU32(rec, r.draw.pipelineId);
    putU32(rec, r.draw.vertexCount);
    putU32(rec, r.draw.fbTarget);
    putU64(rec, r.draw.stateHash);
    if (version >= kVersionCurrent) {
      putU64(rec, r.draw.fbHashAfter);
      putU32(rec, crc32(rec.data(), rec.size()));
    }
    b.insert(b.end(), rec.begin(), rec.end());
  } else if (version >= kVersionCurrent) {
    std::vector<uint8_t> rec;
    putU32(rec, static_cast<uint32_t>(RecordType::kResourceState));
    putU32(rec, r.res.index);
    putStr(rec, r.res.name);
    putU32(rec, r.res.resourceId);
    putU32(rec, r.res.oldState);
    putU32(rec, r.res.newState);
    putU32(rec, crc32(rec.data(), rec.size()));
    b.insert(b.end(), rec.begin(), rec.end());
  }
}

void appendHeader(std::vector<uint8_t>& b, uint32_t version,
                  uint32_t recordCount) {
  putU32(b, kMagic);
  putU32(b, version);
  putU32(b, 0);  // flags
  putU32(b, recordCount);
}

void writeTrace(const std::string& path, const Trace& t) {
  std::vector<uint8_t> b;
  appendHeader(b, t.version,
               t.version == kVersionLegacy
                   ? static_cast<uint32_t>(t.records.size()) : 0);
  for (const Record& r : t.records) appendRecordBytes(b, t.version, r);
  if (t.version >= kVersionCurrent) {
    std::vector<uint8_t> f;
    putU32(f, kFooterMagic);
    putU32(f, static_cast<uint32_t>(t.records.size()));
    putU64(f, t.finalFbHash);
    putU32(f, crc32(f.data(), f.size()));
    b.insert(b.end(), f.begin(), f.end());
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) throw std::runtime_error("cannot write " + path);
  out.write(reinterpret_cast<const char*>(b.data()), b.size());
}

void writeTruncated(const std::string& path, const Trace& full,
                    size_t keepRecords, size_t partialBytes) {
  std::vector<uint8_t> b;
  appendHeader(b, full.version, 0);
  size_t kept = std::min(keepRecords, full.records.size());
  for (size_t i = 0; i < kept; ++i)
    appendRecordBytes(b, full.version, full.records[i]);
  if (kept < full.records.size() && partialBytes > 0) {
    std::vector<uint8_t> next;
    appendRecordBytes(next, full.version, full.records[kept]);
    size_t n = std::min(partialBytes, next.size());
    b.insert(b.end(), next.begin(), next.begin() + n);
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) throw std::runtime_error("cannot write " + path);
  out.write(reinterpret_cast<const char*>(b.data()), b.size());
}

Trace readTrace(const std::string& path, bool strict) {
  std::vector<uint8_t> bytes = readFile(path);
  Reader r{bytes.data(), bytes.size(), 0};
  uint32_t magic, version, flags, count;
  auto fail = [&](const std::string& msg) -> void {
    throw std::runtime_error(path + ": " + msg);
  };
  if (!r.u32(magic) || magic != kMagic) fail("bad magic");
  if (!r.u32(version) || (version != 1 && version != 2))
    fail("unsupported version");
  if (!r.u32(flags) || !r.u32(count)) fail("truncated header");

  Trace t;
  t.version = version;
  uint32_t lastIndex = 0;
  bool haveLast = false;

  while (r.off < r.n) {
    size_t recStart = r.off;
    uint32_t type;
    if (!r.u32(type)) { t.truncated = true; t.droppedBytes += r.n - recStart; break; }
    if (version >= kVersionCurrent && type == kFooterMagic) {
      r.off = recStart;
      uint32_t mg, cnt, fcrc; uint64_t fb;
      if (!r.u32(mg) || !r.u32(cnt) || !r.u64(fb) || !r.u32(fcrc)) {
        if (strict) fail("truncated footer");
        t.truncated = true;
        t.droppedBytes += r.n - recStart;
        break;
      }
      if (crc32(bytes.data() + recStart, 16) != fcrc) {
        if (strict) fail("footer CRC mismatch");
        t.truncated = true;
        break;
      }
      t.finalFbHash = fb;
      if (cnt != t.records.size() && strict) fail("footer count mismatch");
      r.off = r.n;
      break;
    }
    if (type == static_cast<uint32_t>(RecordType::kDraw)) {
      Record rec; rec.type = RecordType::kDraw;
      DrawRecord& d = rec.draw;
      r.off = recStart + 4;
      bool ok = r.u32(d.index) && r.str(d.name) && r.u32(d.pipelineId) &&
                r.u32(d.vertexCount) && r.u32(d.fbTarget) && r.u64(d.stateHash);
      if (ok && version >= kVersionCurrent) {
        uint32_t crc;
        ok = r.u64(d.fbHashAfter) && r.u32(crc) &&
             crc32(bytes.data() + recStart, r.off - recStart - 4) == crc;
      }
      if (!ok) {
        if (strict) fail("bad draw record");
        t.truncated = true;
        t.droppedBytes += r.n - recStart;
        r.off = r.n;
        break;
      }
      lastIndex = d.index; haveLast = true;
      t.records.push_back(std::move(rec));
    } else if (type == static_cast<uint32_t>(RecordType::kResourceState) &&
               version >= kVersionCurrent) {
      Record rec; rec.type = RecordType::kResourceState;
      ResourceStateRecord& s = rec.res;
      r.off = recStart + 4;
      uint32_t crc;
      bool ok = r.u32(s.index) && r.str(s.name) && r.u32(s.resourceId) &&
                r.u32(s.oldState) && r.u32(s.newState) && r.u32(crc) &&
                crc32(bytes.data() + recStart, r.off - recStart - 4) == crc;
      if (!ok) {
        if (strict) fail("bad resource-state record");
        t.truncated = true;
        t.droppedBytes += r.n - recStart;
        r.off = r.n;
        break;
      }
      lastIndex = s.index; haveLast = true;
      t.records.push_back(std::move(rec));
    } else {
      if (strict) fail("unknown record type");
      t.truncated = true;
      t.droppedBytes += r.n - recStart;
      break;
    }
  }
  if (t.truncated) {
    t.recovered = true;
    t.truncatedAtIndex = haveLast ? lastIndex + 1 : 0;
  }
  return t;
}

void truncateFile(const std::string& src, const std::string& dst,
                  size_t keepBytes) {
  std::vector<uint8_t> bytes = readFile(src);
  if (keepBytes > bytes.size()) keepBytes = bytes.size();
  std::ofstream out(dst, std::ios::binary);
  if (!out) throw std::runtime_error("cannot write " + dst);
  out.write(reinterpret_cast<const char*>(bytes.data()), keepBytes);
}

std::string layoutName(uint32_t layout) {
  switch (layout) {
    case 0: return "UNDEFINED";
    case 1: return "GENERAL";
    case 2: return "COLOR_ATTACHMENT_OPTIMAL";
    case 5: return "SHADER_READ_ONLY_OPTIMAL";
    case 6: return "TRANSFER_SRC_OPTIMAL";
    case 7: return "TRANSFER_DST_OPTIMAL";
    default: return "layout-" + std::to_string(layout);
  }
}

}  // namespace trace
