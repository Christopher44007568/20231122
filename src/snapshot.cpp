#include "snapshot.h"

#include <charconv>
#include <cstdio>
#include <map>
#include <sstream>

namespace tar {
namespace {

std::vector<std::string> Split(const std::string& s, char delim) {
  std::vector<std::string> out;
  std::string cur;
  std::istringstream in(s);
  while (std::getline(in, cur, delim)) out.push_back(cur);
  return out;
}

std::string JoinInts(const std::vector<int>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) s += ',';
    s += std::to_string(v[i]);
  }
  return s.empty() ? std::string("-") : s;
}

std::vector<int> ParseInts(const std::string& s) {
  std::vector<int> out;
  if (s.empty() || s == "-") return out;
  for (const auto& tok : Split(s, ',')) out.push_back(std::stoi(tok));
  return out;
}

// Parses "k=v k=v ..." tokens into a map. Returns false on a bad token.
bool ParseKv(const std::vector<std::string>& tokens, size_t start,
             std::map<std::string, std::string>& kv) {
  for (size_t i = start; i < tokens.size(); ++i) {
    auto eq = tokens[i].find('=');
    if (eq == std::string::npos) return false;
    kv[tokens[i].substr(0, eq)] = tokens[i].substr(eq + 1);
  }
  return true;
}

int GetInt(const std::map<std::string, std::string>& kv, const char* key, int dflt) {
  auto it = kv.find(key);
  if (it == kv.end()) return dflt;
  int v = 0;
  auto r = std::from_chars(it->second.data(), it->second.data() + it->second.size(), v);
  if (r.ec != std::errc()) throw PipelineError(std::string("bad int for key ") + key);
  return v;
}

uint64_t GetU64(const std::map<std::string, std::string>& kv, const char* key,
                uint64_t dflt) {
  auto it = kv.find(key);
  if (it == kv.end()) return dflt;
  return std::stoull(it->second, nullptr, 0);
}

std::string GetStr(const std::map<std::string, std::string>& kv, const char* key,
                   const std::string& dflt) {
  auto it = kv.find(key);
  return it == kv.end() ? dflt : it->second;
}

void WriteV1(FILE* f, const Snapshot& s) {
  std::fprintf(f, "TAPS 1\n");
  std::fprintf(f, "meta label=%s frames=%d width=%d height=%d objects=%d\n",
               s.label.c_str(), s.frames, s.width, s.height, s.objects);
  for (const auto& d : s.draws) {
    std::fprintf(f, "draw frame=%d obj=%d layer=%d x=%d y=%d w=%d h=%d\n", d.frame,
                 d.obj, d.layer, d.x, d.y, d.w, d.h);
  }
  for (const auto& r : s.resources) {
    std::fprintf(f, "resource frame=%d resident=%s\n", r.frame,
                 JoinInts(r.resident).c_str());
  }
  for (const auto& fb : s.framebuffers) {
    std::fprintf(f, "framebuffer frame=%d file=%s crc=%08x\n", fb.frame,
                 fb.file.c_str(), fb.crc);
  }
  std::fprintf(f, "end\n");
}

// The line format is space-delimited; free-text fields are sanitized.
std::string Sanitize(const std::string& s) {
  std::string out = s;
  for (auto& c : out) {
    if (c == ' ' || c == '\t') c = '_';
  }
  return out.empty() ? std::string("-") : out;
}

void WriteV2(FILE* f, const Snapshot& s) {
  std::fprintf(f, "TAPS 2\n");
  std::fprintf(f,
               "meta label=%s frames=%d width=%d height=%d objects=%d "
               "extension=%s enabled=%d\n",
               s.label.c_str(), s.frames, s.width, s.height, s.objects,
               s.extension.empty() ? "-" : s.extension.c_str(), s.extension_enabled);
  for (const auto& e : s.events) {
    std::fprintf(f, "event frame=%d kind=%s page=%d obj=%d msg=%s\n", e.frame,
                 e.kind.c_str(), e.page, e.obj,
                 Sanitize(e.message).c_str());
  }
  for (const auto& d : s.draws) {
    std::fprintf(f,
                 "draw frame=%d obj=%d layer=%d page=%d set=%d binding=%d "
                 "placeholder=%d x=%d y=%d w=%d h=%d\n",
                 d.frame, d.obj, d.layer, d.page, d.set, d.binding, d.placeholder,
                 d.x, d.y, d.w, d.h);
  }
  for (const auto& r : s.resources) {
    std::fprintf(f, "resource frame=%d resident=%s evicted=%s membytes=%llu\n",
                 r.frame, JoinInts(r.resident).c_str(), JoinInts(r.evicted).c_str(),
                 static_cast<unsigned long long>(r.membytes));
  }
  for (const auto& fb : s.framebuffers) {
    std::fprintf(f, "framebuffer frame=%d file=%s crc=%08x\n", fb.frame,
                 fb.file.c_str(), fb.crc);
  }
  std::fprintf(f, "end\n");
}

}  // namespace

Snapshot ReadSnapshot(const std::string& path) {
  std::vector<uint8_t> bytes = ReadFile(path);
  std::string text(bytes.begin(), bytes.end());
  std::istringstream in(text);
  std::string line;
  Snapshot snap;
  bool header_seen = false;
  bool end_seen = false;
  int lineno = 0;
  while (std::getline(in, line)) {
    ++lineno;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    const std::string where = path + ":" + std::to_string(lineno);
    try {
      std::vector<std::string> tok = Split(line, ' ');
      if (!header_seen) {
        if (tok.size() != 2 || tok[0] != "TAPS") {
          throw PipelineError("bad header, expected 'TAPS <version>'");
        }
        snap.format_version = std::stoi(tok[1]);
        if (snap.format_version != 1 && snap.format_version != 2) {
          throw PipelineError("unsupported TAPS version " + tok[1]);
        }
        header_seen = true;
        continue;
      }
      std::map<std::string, std::string> kv;
      if (tok[0] == "end") {
        end_seen = true;
        continue;
      }
      if (!ParseKv(tok, 1, kv)) {
        throw PipelineError("malformed key=value tokens");
      }
      if (tok[0] == "meta") {
        snap.label = GetStr(kv, "label", "?");
        snap.frames = GetInt(kv, "frames", 0);
        snap.width = GetInt(kv, "width", 0);
        snap.height = GetInt(kv, "height", 0);
        snap.objects = GetInt(kv, "objects", 0);
        if (snap.format_version >= 2) {
          snap.extension = GetStr(kv, "extension", "");
          if (snap.extension == "-") snap.extension.clear();
          snap.extension_enabled = GetInt(kv, "enabled", 0);
        }
      } else if (tok[0] == "draw") {
        DrawCall d;
        d.frame = GetInt(kv, "frame", 0);
        d.obj = GetInt(kv, "obj", 0);
        d.layer = GetInt(kv, "layer", 0);
        d.x = GetInt(kv, "x", 0);
        d.y = GetInt(kv, "y", 0);
        d.w = GetInt(kv, "w", 0);
        d.h = GetInt(kv, "h", 0);
        if (snap.format_version >= 2) {
          d.page = GetInt(kv, "page", d.layer);
          d.set = GetInt(kv, "set", 0);
          d.binding = GetInt(kv, "binding", 0);
          d.placeholder = GetInt(kv, "placeholder", 0);
        } else {
          d.page = d.layer;  // legacy format conflated page and layer
        }
        snap.draws.push_back(d);
      } else if (tok[0] == "resource") {
        ResourceState r;
        r.frame = GetInt(kv, "frame", 0);
        r.resident = ParseInts(GetStr(kv, "resident", "-"));
        if (snap.format_version >= 2) {
          r.evicted = ParseInts(GetStr(kv, "evicted", "-"));
          r.membytes = GetU64(kv, "membytes", 0);
        }
        snap.resources.push_back(r);
      } else if (tok[0] == "event") {
        if (snap.format_version < 2) {
          snap.warnings.push_back(where + ": event record ignored in v1 file");
          continue;
        }
        PageEvent e;
        e.frame = GetInt(kv, "frame", 0);
        e.kind = GetStr(kv, "kind", "unknown");
        e.page = GetInt(kv, "page", -1);
        e.obj = GetInt(kv, "obj", -1);
        e.message = GetStr(kv, "msg", "");
        snap.events.push_back(e);
      } else if (tok[0] == "framebuffer") {
        FramebufferRef fb;
        fb.frame = GetInt(kv, "frame", 0);
        fb.file = GetStr(kv, "file", "");
        fb.crc = static_cast<uint32_t>(std::stoull(GetStr(kv, "crc", "0"), nullptr, 16));
        snap.framebuffers.push_back(fb);
      } else {
        // Forward compatibility: unknown record kinds are not fatal.
        snap.warnings.push_back(where + ": unknown record '" + tok[0] + "' skipped");
      }
    } catch (const std::exception& e) {
      // Error resilience: a bad record never aborts the whole parse.
      snap.warnings.push_back(where + ": " + e.what() + " (record skipped)");
    }
  }
  if (!header_seen) throw PipelineError("empty or headerless snapshot: " + path);
  if (!end_seen) snap.warnings.push_back(path + ": missing 'end' record");
  return snap;
}

void WriteSnapshot(const std::string& path, const Snapshot& snap, int format_version) {
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) throw PipelineError("cannot write snapshot: " + path);
  if (format_version == 1) {
    WriteV1(f, snap);
  } else if (format_version == 2) {
    WriteV2(f, snap);
  } else {
    std::fclose(f);
    throw PipelineError("unknown snapshot format version: " +
                        std::to_string(format_version));
  }
  std::fclose(f);
}

}  // namespace tar
