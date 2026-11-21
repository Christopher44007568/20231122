#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gcdump {

enum class Severity { Info, Warning, Error };

enum class Verdict {
    AutoConfirmed,
    AutoRejected,
    Indeterminate,
    NeedsManualReview
};

const char* to_string(Severity s);
const char* to_string(Verdict v);

// One physical line of the source dump. sourceLine is 1-based and must never
// be rewritten or regenerated: it anchors every conclusion back to the input.
struct SourceLine {
    std::size_t sourceLine = 0;
    std::string raw;
};

struct FieldRecord {
    std::string key;
    std::string value;
    std::size_t sourceLine = 0;
};

struct ParseIssue {
    Severity severity = Severity::Error;
    std::size_t sourceLine = 0;
    std::string code;
    std::string message;
};

// Pointer to the evidence a conclusion rests on.
struct EvidenceRef {
    std::size_t sourceLine = 0;
    std::string quote;
};

enum class PipelineStage {
    Ingest,
    Parse,
    Normalize,
    Classify,
    Correlate,
    Conclude
};

const char* to_string(PipelineStage stage);

// Immutable audit entry for one processing step.
struct StepRecord {
    PipelineStage stage = PipelineStage::Ingest;
    std::size_t order = 0;
    std::string action;
    std::string detail;
    std::vector<EvidenceRef> inputs;
    std::vector<EvidenceRef> outputs;
};

struct Finding {
    std::string id;
    std::string subject;
    Verdict verdict = Verdict::NeedsManualReview;
    std::string summary;
    std::string rationale;
    std::vector<EvidenceRef> evidence;
    std::vector<std::size_t> steps;
};

struct FollowUpAction {
    std::string action;
    std::string rationale;
    bool blocksReliability = false;
};

struct FailureReport {
    std::string code;
    std::string message;
    std::vector<EvidenceRef> evidence;
    std::vector<FollowUpAction> actions;
};

struct ManualCheck {
    std::string item;
    std::string reason;
    std::vector<EvidenceRef> evidence;
    std::vector<FollowUpAction> actions;
};

struct Report {
    std::string dumpId;
    std::string sourcePath;
    std::vector<SourceLine> lines;
    std::vector<FieldRecord> fields;
    std::vector<ParseIssue> issues;
    std::vector<StepRecord> steps;
    std::vector<Finding> findings;
    std::vector<ManualCheck> manualChecks;
    std::vector<FailureReport> failures;
    std::size_t manualBoundaryLine = 0;

    bool hasErrors() const;
    const SourceLine* findLine(std::size_t lineNo) const;
    std::optional<FieldRecord> field(std::string_view key) const;
};

} // namespace gcdump
