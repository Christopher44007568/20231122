#pragma once
#include <string>
#include <vector>
#include <cstdio>

// One recorded experiment step: input, state before/after, result.
struct TraceEntry {
  int seq = 0;
  std::string runId;     // e.g. "record", "replay", "reorder", "duplicate"
  std::string step;      // step name
  std::string input;     // JSON object fragment (already escaped)
  std::string stateHash; // hash of relevant state before/after execution
  std::string result;    // JSON object fragment
  std::string status;    // "ok" | "fail" | "expected_fail"
  std::string note;
};

// Append-only JSONL trace writer.
class TraceLog {
public:
  bool open(const std::string& path, bool append);
  void write(const TraceEntry& e);
  void close();
  int nextSeq() { return seq_++; }

private:
  FILE* f_ = nullptr;
  int seq_ = 0;
};

std::string jsonEscape(const std::string& s);
