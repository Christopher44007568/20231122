#include "report.h"

#include <cstdio>
#include <cstring>

namespace rpsc {

std::string jsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      default: o += c;
    }
  }
  return o;
}

std::string hex64(uint64_t v) {
  char b[17];
  std::snprintf(b, sizeof(b), "%016llx", (unsigned long long)v);
  return b;
}

static void writeSnapshot(FILE* f, const OpSnapshot& s, bool last) {
  std::fprintf(f,
               "    {\"seq\":%zu,\"op\":\"%s\",\"pipeline\":%u,\"blend\":%u,"
               "\"cull\":%u,\"mask\":%u,\"viewport\":[%u,%u],"
               "\"colorImageLayout\":%u,\"stateHash\":\"%s\",",
               s.seq, opName(s.type), s.pipelineId, s.blendEnable, s.cullMode,
               s.colorWriteMask, s.viewportW, s.viewportH, s.colorImageLayout,
               hex64(s.stateHash).c_str());
  if (s.hasFbHash)
    std::fprintf(f, "\"fbHash\":\"%s\",", hex64(s.fbHash).c_str());
  std::fprintf(f, "\"layoutError\":%s", s.layoutError ? "true" : "false");
  if (!s.note.empty())
    std::fprintf(f, ",\"note\":\"%s\"", jsonEscape(s.note).c_str());
  std::fprintf(f, "}%s\n", last ? "" : ",");
}

bool writeStateStreamJson(const std::string& path, const ReplayResult& rr,
                          std::string* err) {
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) {
    *err = "cannot write " + path;
    return false;
  }
  std::fprintf(f,
               "{\n  \"cacheHits\":%u,\n  \"cacheMisses\":%u,\n"
               "  \"drawsExecuted\":%u,\n  \"glfwAvailable\":%s,\n  \"ops\":[\n",
               rr.cacheHits, rr.cacheMisses, rr.drawsExecuted,
               rr.glfwAvailable ? "true" : "false");
  for (size_t i = 0; i < rr.ops.size(); ++i)
    writeSnapshot(f, rr.ops[i], i + 1 == rr.ops.size());
  std::fprintf(f, "  ]\n}\n");
  std::fclose(f);
  return true;
}

// Minimal JSON reader for the files we write (tolerant whitespace scan).
static bool scanUint(const std::string& j, const char* key, uint64_t* out,
                     size_t from, size_t limit) {
  std::string pat = std::string("\"") + key + "\":";
  size_t p = j.find(pat, from);
  if (p == std::string::npos || p >= limit) return false;
  p += pat.size();
  *out = std::strtoull(j.c_str() + p, nullptr, 0);
  return true;
}

static bool scanHex(const std::string& j, const char* key, uint64_t* out,
                    size_t from, size_t limit) {
  std::string pat = std::string("\"") + key + "\":\"";
  size_t p = j.find(pat, from);
  if (p == std::string::npos || p >= limit) return false;
  p += pat.size();
  *out = std::strtoull(j.c_str() + p, nullptr, 16);
  return true;
}

static bool scanStr(const std::string& j, const char* key, std::string* out,
                    size_t from, size_t limit) {
  std::string pat = std::string("\"") + key + "\":\"";
  size_t p = j.find(pat, from);
  if (p == std::string::npos || p >= limit) return false;
  p += pat.size();
  size_t e = j.find('"', p);
  *out = j.substr(p, e - p);
  return true;
}

bool readStateStreamJson(const std::string& path, ReplayResult* rr,
                         std::string* err) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    *err = "cannot open " + path;
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::string j(size_t(n), '\0');
  bool ok = std::fread(&j[0], 1, j.size(), f) == j.size();
  std::fclose(f);
  if (!ok) {
    *err = "short read " + path;
    return false;
  }
  uint64_t v = 0;
  if (scanUint(j, "cacheHits", &v, 0, j.size())) rr->cacheHits = uint32_t(v);
  if (scanUint(j, "cacheMisses", &v, 0, j.size())) rr->cacheMisses = uint32_t(v);
  if (scanUint(j, "drawsExecuted", &v, 0, j.size()))
    rr->drawsExecuted = uint32_t(v);
  size_t pos = 0;
  while (true) {
    size_t obj = j.find("{\"seq\":", pos);
    if (obj == std::string::npos) break;
    size_t objEnd = j.find('}', obj);
    if (objEnd == std::string::npos) break;
    OpSnapshot s;
    scanUint(j, "seq", &v, obj, objEnd);
    s.seq = size_t(v);
    std::string name;
    if (scanStr(j, "op", &name, obj, objEnd)) {
      for (int t = 1; t <= 8; ++t)
        if (name == opName(OpType(t))) s.type = OpType(t);
    }
    if (scanUint(j, "pipeline", &v, obj, objEnd)) s.pipelineId = uint32_t(v);
    if (scanUint(j, "blend", &v, obj, objEnd)) s.blendEnable = uint32_t(v);
    if (scanUint(j, "cull", &v, obj, objEnd)) s.cullMode = uint32_t(v);
    if (scanUint(j, "mask", &v, obj, objEnd)) s.colorWriteMask = uint32_t(v);
    size_t vp = j.find("\"viewport\":[", obj);
    if (vp != std::string::npos && vp < objEnd) {
      s.viewportW = uint32_t(std::strtoul(j.c_str() + vp + 12, nullptr, 10));
      const char* comma = std::strchr(j.c_str() + vp + 12, ',');
      if (comma) s.viewportH = uint32_t(std::strtoul(comma + 1, nullptr, 10));
    }
    if (scanUint(j, "colorImageLayout", &v, obj, objEnd))
      s.colorImageLayout = uint32_t(v);
    if (scanHex(j, "stateHash", &v, obj, objEnd)) s.stateHash = v;
    if (scanHex(j, "fbHash", &v, obj, objEnd)) {
      s.fbHash = v;
      s.hasFbHash = true;
    }
    s.layoutError = j.find("\"layoutError\":true", obj) < objEnd;
    scanStr(j, "note", &s.note, obj, objEnd);
    rr->ops.push_back(s);
    pos = objEnd + 1;
  }
  return true;
}

static void cmpField(std::vector<std::string>* out, const char* name,
                     uint64_t e, uint64_t a) {
  if (e != a) {
    char b[128];
    std::snprintf(b, sizeof(b), "%s: expected %llu, actual %llu", name,
                  (unsigned long long)e, (unsigned long long)a);
    out->push_back(b);
  }
}

Divergence findFirstDivergence(const ReplayResult& golden,
                               const ReplayResult& actual) {
  Divergence d;
  // Align on state-changing ops only: Checkpoint records are verification
  // anchors and exist solely in v2 streams, so including them would skew
  // the comparison against legacy (v1) streams.
  std::vector<const OpSnapshot*> ge, ae;
  for (const OpSnapshot& s : golden.ops)
    if (s.type != OpType::Checkpoint) ge.push_back(&s);
  for (const OpSnapshot& s : actual.ops)
    if (s.type != OpType::Checkpoint) ae.push_back(&s);
  size_t n = ge.size() < ae.size() ? ge.size() : ae.size();
  for (size_t i = 0; i < n; ++i) {
    const OpSnapshot& e = *ge[i];
    const OpSnapshot& a = *ae[i];
    std::vector<std::string> fields;
    if (e.type != a.type) {
      fields.push_back(std::string("op: expected ") + opName(e.type) +
                       ", actual " + opName(a.type));
    }
    cmpField(&fields, "pipeline", e.pipelineId, a.pipelineId);
    cmpField(&fields, "blend", e.blendEnable, a.blendEnable);
    cmpField(&fields, "cull", e.cullMode, a.cullMode);
    cmpField(&fields, "colorWriteMask", e.colorWriteMask, a.colorWriteMask);
    cmpField(&fields, "viewportW", e.viewportW, a.viewportW);
    cmpField(&fields, "viewportH", e.viewportH, a.viewportH);
    cmpField(&fields, "colorImageLayout", e.colorImageLayout, a.colorImageLayout);
    cmpField(&fields, "stateHash", e.stateHash, a.stateHash);
    if (e.hasFbHash || a.hasFbHash) {
      if (e.hasFbHash != a.hasFbHash)
        fields.push_back("fbHash: presence differs");
      else if (e.fbHash != a.fbHash)
        fields.push_back("fbHash: expected " + hex64(e.fbHash) + ", actual " +
                         hex64(a.fbHash));
    }
    if (!fields.empty()) {
      d.found = true;
      d.seq = i;
      d.expected = e;
      d.actual = a;
      d.hasExpected = d.hasActual = true;
      d.fields = fields;
      return d;
    }
  }
  if (ge.size() != ae.size()) {
    d.found = true;
    d.seq = n;
    if (n < ge.size()) {
      d.expected = *ge[n];
      d.hasExpected = true;
      d.fields.push_back(std::string("op ") + opName(d.expected.type) +
                         " missing: actual stream ends here (expected " +
                         std::to_string(ge.size()) + " effective ops, got " +
                         std::to_string(ae.size()) + ")");
    } else {
      d.actual = *ae[n];
      d.hasActual = true;
      d.fields.push_back("unexpected extra op in actual stream");
    }
  }
  return d;
}

static void writeCompactSnapshot(FILE* f, const OpSnapshot& s) {
  std::fprintf(f,
               "{\"seq\":%zu,\"op\":\"%s\",\"pipeline\":%u,"
               "\"colorImageLayout\":%u,\"stateHash\":\"%s\"",
               s.seq, opName(s.type), s.pipelineId, s.colorImageLayout,
               hex64(s.stateHash).c_str());
  if (s.hasFbHash) std::fprintf(f, ",\"fbHash\":\"%s\"", hex64(s.fbHash).c_str());
  std::fprintf(f, "}");
}

bool writeReplayReportJson(const std::string& path, const ReplayResult& golden,
                           const ReplayResult& actual, const Divergence& div,
                           const std::vector<std::string>& evidenceFiles,
                           std::string* err) {
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) {
    *err = "cannot write " + path;
    return false;
  }
  std::fprintf(f, "{\n");
  std::fprintf(f, "  \"goldenOps\":%zu,\n  \"actualOps\":%zu,\n",
               golden.ops.size(), actual.ops.size());
  std::fprintf(f, "  \"cacheHits\":%u,\n  \"cacheMisses\":%u,\n",
               actual.cacheHits, actual.cacheMisses);
  std::fprintf(f, "  \"checkpointMismatches\":%zu,\n",
               actual.checkpointMismatches.size());
  std::fprintf(f, "  \"firstErroneousState\":");
  if (!div.found) {
    std::fprintf(f, " null,\n");
  } else {
    std::fprintf(f, " {\n    \"seq\":%zu,\n    \"differences\":[\n", div.seq);
    for (size_t i = 0; i < div.fields.size(); ++i)
      std::fprintf(f, "      \"%s\"%s\n", jsonEscape(div.fields[i]).c_str(),
                   i + 1 == div.fields.size() ? "" : ",");
    std::fprintf(f, "    ],\n");
    std::fprintf(f, "    \"expected\": ");
    if (div.hasExpected) writeCompactSnapshot(f, div.expected);
    else std::fprintf(f, "null");
    std::fprintf(f, ",\n    \"actual\": ");
    if (div.hasActual) writeCompactSnapshot(f, div.actual);
    else std::fprintf(f, "null");
    std::fprintf(f, "\n");
    std::fprintf(f, "  },\n");
  }
  std::fprintf(f, "  \"evidence\":[\n");
  for (size_t i = 0; i < evidenceFiles.size(); ++i)
    std::fprintf(f, "    \"%s\"%s\n", jsonEscape(evidenceFiles[i]).c_str(),
                 i + 1 == evidenceFiles.size() ? "" : ",");
  std::fprintf(f, "  ]\n}\n");
  std::fclose(f);
  return true;
}

}  // namespace rpsc
