#pragma once
#include "renderer.h"

#include <string>
#include <vector>

namespace rpsc {

std::string jsonEscape(const std::string& s);
std::string hex64(uint64_t v);

// Serializes a replayed op stream as JSON (state + framebuffer hashes).
bool writeStateStreamJson(const std::string& path, const ReplayResult& rr,
                          std::string* err);
bool readStateStreamJson(const std::string& path, ReplayResult* rr,
                         std::string* err);

struct Divergence {
  bool found = false;
  size_t seq = 0;
  OpSnapshot expected;
  OpSnapshot actual;
  bool hasExpected = false;  // false => op missing in actual stream
  bool hasActual = false;
  std::vector<std::string> fields;  // human-readable field differences
};

// Aligns both streams by sequence and finds the FIRST divergent state,
// with field-level detail. This is the core of the review: pinpoint the
// first wrong state instead of reporting an aggregate score.
Divergence findFirstDivergence(const ReplayResult& golden,
                               const ReplayResult& actual);

// Writes the machine-readable replay comparison report.
bool writeReplayReportJson(const std::string& path, const ReplayResult& golden,
                           const ReplayResult& actual, const Divergence& div,
                           const std::vector<std::string>& evidenceFiles,
                           std::string* err);

}  // namespace rpsc
