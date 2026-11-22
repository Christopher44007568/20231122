#include "analyze.h"

#include <algorithm>
namespace analyze {

const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::OK: return "OK";
        case Verdict::ERROR: return "ERROR";
        case Verdict::UNDECIDABLE: return "UNDECIDABLE";
    }
    return "?";
}

static uint64_t u64(const util::JsonObj& o, const char* key, uint64_t fallback = 0) {
    auto it = o.find(key);
    if (it == o.end() || it->second.empty()) return fallback;
    return std::stoull(it->second);
}

Analysis run(const std::vector<tslog::Record>& records,
             const TruncationEvent& truncation,
             float timestamp_period_ns,
             uint64_t budget_ns) {
    Analysis out;
    int next_id = 1;

    for (const auto& rec : records) {
        Conclusion c;
        c.id = next_id++;
        c.byte_offset = rec.byte_offset;
        c.seq = rec.seq;
        c.raw_excerpt = rec.raw_line;
        c.step_id = "P6-validate";

        if (rec.kind == "meta") {
            c.subject = "meta: run environment";
            c.verdict = Verdict::OK;
            c.detail = "environment record committed to chain";
        } else if (rec.kind == "sample") {
            uint64_t avail = u64(rec.fields, "avail");
            uint64_t begin = u64(rec.fields, "ts_begin");
            uint64_t end = u64(rec.fields, "ts_end");
            c.subject = "frame " + std::to_string(u64(rec.fields, "frame")) +
                        " (" + rec.fields.at("origin") + ")";
            if ((avail & 1u) == 0) {
                c.verdict = Verdict::ERROR;
                c.detail = "begin timestamp unavailable: frame cannot be placed on the GPU timeline";
            } else if ((avail & 2u) == 0) {
                // The undecidable boundary: half the interval is missing, so
                // no duration exists and the budget question has no answer.
                c.verdict = Verdict::UNDECIDABLE;
                c.detail = "end timestamp unavailable (avail=" + std::to_string(avail) +
                           "): duration does not exist, budget compliance is undecidable";
            } else if (end < begin) {
                c.verdict = Verdict::ERROR;
                c.detail = "non-monotonic timestamps: ts_end < ts_begin";
            } else {
                double duration_ns = static_cast<double>(end - begin) * timestamp_period_ns;
                c.detail = "duration " + std::to_string(static_cast<uint64_t>(duration_ns)) +
                           " ns (budget " + std::to_string(budget_ns) + " ns)";
                if (duration_ns > static_cast<double>(budget_ns)) {
                    c.verdict = Verdict::ERROR;
                    c.detail += " -> budget exceeded";
                } else {
                    c.verdict = Verdict::OK;
                    c.detail += " -> within budget";
                }
            }
        } else if (rec.kind == "replay-note") {
            c.subject = "recovery replay marker";
            c.verdict = Verdict::OK;
            c.detail = "replay boundary committed: " +
                       (rec.fields.count("note") ? rec.fields.at("note") : "");
        } else {
            c.subject = "unknown record kind '" + rec.kind + "'";
            c.verdict = Verdict::ERROR;
            c.detail = "unrecognized record kind";
        }

        out.conclusions.push_back(c);
    }

    if (truncation.present) {
        Conclusion c;
        c.id = next_id++;
        c.byte_offset = truncation.byte_offset;
        c.seq = truncation.expected_seq;
        c.subject = "truncated record (crash mid-write)";
        c.verdict = Verdict::ERROR;
        c.detail = "uncommitted tail at offset " + std::to_string(truncation.byte_offset) +
                   ": " + truncation.reason +
                   "; salvaged " + std::to_string(truncation.salvaged_bytes.size()) +
                   " bytes preserved in " + truncation.evidence_file;
        c.raw_excerpt = truncation.salvaged_bytes;
        c.step_id = "P4-recover-scan";
        out.conclusions.push_back(c);
    }

    // Order by raw-stream position so "first error" is a stream property.
    std::stable_sort(out.conclusions.begin(), out.conclusions.end(),
                     [](const Conclusion& a, const Conclusion& b) {
                         return a.byte_offset < b.byte_offset;
                     });
    for (size_t i = 0; i < out.conclusions.size(); i++) {
        out.conclusions[i].id = static_cast<int>(i) + 1;
        switch (out.conclusions[i].verdict) {
            case Verdict::OK: out.count_ok++; break;
            case Verdict::ERROR:
                out.count_error++;
                if (out.first_error_index < 0) out.first_error_index = static_cast<int>(i);
                break;
            case Verdict::UNDECIDABLE: out.count_undecidable++; break;
        }
    }
    return out;
}

}  // namespace analyze
