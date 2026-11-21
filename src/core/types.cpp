#include "types.hpp"

#include <algorithm>

namespace gcdump {

const char* to_string(Severity s) {
    switch (s) {
    case Severity::Info: return "INFO";
    case Severity::Warning: return "WARNING";
    case Severity::Error: return "ERROR";
    }
    return "UNKNOWN";
}

const char* to_string(Verdict v) {
    switch (v) {
    case Verdict::AutoConfirmed: return "AUTO_CONFIRMED";
    case Verdict::AutoRejected: return "AUTO_REJECTED";
    case Verdict::Indeterminate: return "INDETERMINATE";
    case Verdict::NeedsManualReview: return "NEEDS_MANUAL_REVIEW";
    }
    return "UNKNOWN";
}

const char* to_string(PipelineStage stage) {
    switch (stage) {
    case PipelineStage::Ingest: return "1-INGEST";
    case PipelineStage::Parse: return "2-PARSE";
    case PipelineStage::Normalize: return "3-NORMALIZE";
    case PipelineStage::Classify: return "4-CLASSIFY";
    case PipelineStage::Correlate: return "5-CORRELATE";
    case PipelineStage::Conclude: return "6-CONCLUDE";
    }
    return "UNKNOWN";
}

bool Report::hasErrors() const {
    return std::any_of(issues.begin(), issues.end(),
                       [](const ParseIssue& i) { return i.severity == Severity::Error; });
}

const SourceLine* Report::findLine(std::size_t lineNo) const {
    auto it = std::find_if(lines.begin(), lines.end(),
                           [lineNo](const SourceLine& l) { return l.sourceLine == lineNo; });
    return it == lines.end() ? nullptr : &*it;
}

std::optional<FieldRecord> Report::field(std::string_view key) const {
    auto it = std::find_if(fields.begin(), fields.end(),
                           [&](const FieldRecord& f) { return f.key == key; });
    if (it == fields.end()) return std::nullopt;
    return *it;
}

} // namespace gcdump
