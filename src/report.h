#pragma once
#include <string>
#include <vector>
#include "png.h"
#include "trace.h"

// Per-record comparison row linking draw calls, resource states and
// framebuffer hashes between the reference capture and the replayed
// (possibly recovered) candidate.
struct CompareRow {
  uint32_t index = 0;
  std::string kind;     // "draw" | "resource"
  std::string name;
  bool missing = false; // present in reference, absent in candidate
  bool stateOk = true;  // draw state hash / resource transition match
  bool fbOk = true;     // framebuffer hash after draw (v2 only)
  uint64_t expectedState = 0, actualState = 0;
  uint64_t expectedFb = 0, actualFb = 0;
  std::string detail;
};

struct FirstError {
  bool found = false;
  uint32_t index = 0;
  std::string kind;    // missing-draw | state-mismatch | fb-mismatch |
                       // resource-state-mismatch | missing-resource-state
  std::string summary;
};

struct ReviewResult {
  uint32_t referenceVersion = 0;
  uint32_t candidateVersion = 0;
  bool candidateRecovered = false;
  uint32_t truncatedAtIndex = 0;
  size_t droppedBytes = 0;
  std::vector<CompareRow> rows;
  FirstError firstError;
  uint64_t finalDiffPixels = 0;
  uint32_t finalMaxDiff = 0;
  uint64_t referenceFinalHash = 0;
  uint64_t candidateFinalHash = 0;
};

// Compares reference records against candidate records + replay hashes.
// replayFb hashes are ordered like the candidate's draw records.
// referenceFinal/candidateFinal are the rendered final frames (may be
// empty if unavailable).
ReviewResult buildReview(const trace::Trace& reference,
                         const trace::Trace& candidate,
                         const std::vector<uint64_t>& replayFb,
                         uint64_t candidateFinalHash,
                         const Image& referenceFinal,
                         const Image& candidateFinal,
                         Image* diffOut);

void writeReportJson(const std::string& path, const ReviewResult& r,
                     const std::vector<std::string>& evidenceFiles);
