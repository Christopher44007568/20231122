#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "log.h"

namespace analyze {

enum class Verdict { OK, ERROR, UNDECIDABLE };

const char* verdict_name(Verdict v);

struct TruncationEvent {
    bool present = false;
    uint64_t byte_offset = 0;
    std::string salvaged_bytes;
    std::string reason;
    uint64_t expected_seq = 0;
    std::string evidence_file;  // path where salvaged bytes were preserved
};

// One conclusion drawn by the analyzer, anchored to raw input.
struct Conclusion {
    int id = 0;
    uint64_t byte_offset = 0;   // anchor into the raw log stream
    uint64_t seq = 0;
    std::string subject;        // what this conclusion is about
    Verdict verdict = Verdict::OK;
    std::string detail;         // human-readable reasoning
    std::string raw_excerpt;    // verbatim raw input the conclusion rests on
    std::string step_id;        // processing step that produced it
};

struct Analysis {
    std::vector<Conclusion> conclusions;
    int first_error_index = -1;  // index into conclusions, -1 if none
    size_t count_ok = 0;
    size_t count_error = 0;
    size_t count_undecidable = 0;
};

// Evaluates every recovered record plus the truncation event, ordered by
// byte offset, so the first error state is the earliest broken state in
// the raw stream rather than an aggregate.
Analysis run(const std::vector<tslog::Record>& records,
             const TruncationEvent& truncation,
             float timestamp_period_ns,
             uint64_t budget_ns);

}  // namespace analyze
