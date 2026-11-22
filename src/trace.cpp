#include "trace.h"

std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

bool TraceLog::open(const std::string& path, bool append) {
  f_ = std::fopen(path.c_str(), append ? "a" : "w");
  return f_ != nullptr;
}

void TraceLog::write(const TraceEntry& e) {
  if (!f_) return;
  std::fprintf(f_,
    "{\"seq\":%d,\"run\":\"%s\",\"step\":\"%s\",\"input\":%s,"
    "\"state_hash\":\"%s\",\"result\":%s,\"status\":\"%s\",\"note\":\"%s\"}\n",
    e.seq, jsonEscape(e.runId).c_str(), jsonEscape(e.step).c_str(),
    e.input.empty() ? "{}" : e.input.c_str(), e.stateHash.c_str(),
    e.result.empty() ? "{}" : e.result.c_str(), e.status.c_str(),
    jsonEscape(e.note).c_str());
  std::fflush(f_);
}

void TraceLog::close() {
  if (f_) {
    std::fclose(f_);
    f_ = nullptr;
  }
}
