#include "analyzer.hpp"
#include "parser.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace gcdump {

namespace {

EvidenceRef quoteLine(const Report& r, std::size_t lineNo) {
    if (const SourceLine* l = r.findLine(lineNo)) {
        return EvidenceRef{lineNo, l->raw};
    }
    return EvidenceRef{lineNo, "<line not found>"};
}

class StepLog {
public:
    explicit StepLog(Report& r) : report_(r) {}

    std::size_t add(PipelineStage stage, std::string action, std::string detail,
                    std::vector<EvidenceRef> in = {},
                    std::vector<EvidenceRef> out = {}) {
        StepRecord s;
        s.stage = stage;
        s.order = report_.steps.size() + 1;
        s.action = std::move(action);
        s.detail = std::move(detail);
        s.inputs = std::move(in);
        s.outputs = std::move(out);
        report_.steps.push_back(std::move(s));
        return report_.steps.back().order;
    }

private:
    Report& report_;
};
} // namespace

std::vector<ExtensionInfo> extractExtensions(const Report& report) {
    std::vector<ExtensionInfo> out;
    for (const FieldRecord& f : report.fields) {
        if (f.key != "extension") continue;
        ExtensionInfo info;
        info.rawValue = f.value;
        info.sourceLine = f.sourceLine;

        std::string token;
        std::istringstream ss(f.value);
        bool first = true;
        while (ss >> token) {
            if (first) {
                info.name = token;
                first = false;
            } else if (token.rfind("status=", 0) == 0) {
                info.status = token.substr(7);
            }
        }
        out.push_back(std::move(info));
    }
    return out;
}

namespace {

// Knowledge base of extensions the dump pipeline can classify on its own.
// Anything not listed here is deliberately treated as unknown evidence.
const std::set<std::string>& knownExtensions() {
    static const std::set<std::string> known = {
        "VK_KHR_swapchain",
        "VK_KHR_surface",
        "VK_KHR_portability_enumeration",
        "VK_EXT_debug_utils",
        "VK_EXT_memory_budget",
    };
    return known;
}

std::string normalizeName(std::string name) {
    return trim(name);
}

std::string normalizeStatus(std::string status) {
    return lower(trim(status));
}

bool statusSupported(const std::string& status) {
    return status == "enabled" || status == "supported" || status == "active";
}

void addFinding(Report& report, std::string id, std::string subject,
                Verdict verdict, std::string summary, std::string rationale,
                std::vector<EvidenceRef> evidence,
                std::vector<std::size_t> steps) {
    Finding f;
    f.id = std::move(id);
    f.subject = std::move(subject);
    f.verdict = verdict;
    f.summary = std::move(summary);
    f.rationale = std::move(rationale);
    f.evidence = std::move(evidence);
    f.steps = std::move(steps);
    report.findings.push_back(std::move(f));
}

void analyzeNormalized(Report& report, StepLog& log,
                       std::size_t ingestStep, std::size_t parseStep);
void analyzeReplay(Report& report, StepLog& log, std::size_t parseStep,
                   std::size_t normStep,
                   const std::vector<ExtensionInfo>& exts);
void analyzeExtensions(Report& report, StepLog& log, std::size_t parseStep,
                       std::size_t normStep,
                       const std::vector<ExtensionInfo>& exts,
                       const std::vector<std::size_t>& replaySteps);

} // namespace

Report analyze(ParseResult parsed, std::string sourcePath) {
    Report report;
    report.sourcePath = std::move(sourcePath);
    report.lines = std::move(parsed.lines);
    report.fields = std::move(parsed.fields);
    report.issues = std::move(parsed.issues);

    if (auto id = report.field("dump_id")) report.dumpId = id->value;

    StepLog log(report);
    std::vector<EvidenceRef> allRefs;
    allRefs.reserve(report.lines.size());
    for (const auto& l : report.lines) {
        allRefs.push_back(EvidenceRef{l.sourceLine, l.raw});
    }

    std::size_t ingestStep = log.add(
        PipelineStage::Ingest, "read source dump",
        std::to_string(report.lines.size()) + " line(s) read without rewrite",
        {}, allRefs);

    std::vector<EvidenceRef> parseOutputs;
    for (const auto& f : report.fields) {
        parseOutputs.push_back(quoteLine(report, f.sourceLine));
    }
    std::size_t parseStep = log.add(
        PipelineStage::Parse, "tokenize key/value lines",
        std::to_string(report.fields.size()) + " field record(s), " +
            std::to_string(report.issues.size()) + " issue(s)",
        allRefs, parseOutputs);

    if (report.hasErrors()) {
        FailureReport failure;
        failure.code = "F1001";
        failure.message = "unparseable input: provenance chain is broken";
        for (const auto& issue : report.issues) {
            if (issue.severity == Severity::Error) {
                failure.evidence.push_back(quoteLine(report, issue.sourceLine));
            }
        }
        failure.actions.push_back(
            {"repair the malformed line(s) in the source dump and re-run the "
             "checker",
             "the parser cannot anchor conclusions to an unreadable line",
             true});
        failure.actions.push_back(
            {"re-export the dump from the capture tool with key: value lines",
             "restores a fully traceable input format", true});
        report.failures.push_back(std::move(failure));
        log.add(PipelineStage::Conclude, "abort after parse failure",
                "no auto conclusions emitted; every issue retains its line",
                parseOutputs, {});
        return report;
    }

    analyzeNormalized(report, log, ingestStep, parseStep);
    return report;
}

namespace {

void analyzeNormalized(Report& report, StepLog& log,
                       std::size_t /*ingestStep*/, std::size_t parseStep) {
    // Stage 3: normalize names/status values while keeping source lines.
    auto exts = extractExtensions(report);
    std::vector<EvidenceRef> extRefs;
    for (const auto& e : exts) extRefs.push_back(quoteLine(report, e.sourceLine));
    std::size_t normStep = log.add(
        PipelineStage::Normalize, "trim names and lowercase status",
        std::to_string(exts.size()) + " extension record(s) normalized",
        extRefs, extRefs);

    // Required identity fields: their absence blocks reliable conclusions.
    static const char* requiredKeys[] = {"dump_id", "device_name",
                                         "driver_version", "checksum_expected",
                                         "checksum_actual"};
    std::vector<EvidenceRef> missingRefs;
    std::vector<std::string> missingKeys;
    for (const char* key : requiredKeys) {
        if (!report.field(key)) {
            missingKeys.emplace_back(key);
            missingRefs.push_back(EvidenceRef{0, std::string("missing key: ") + key});
        }
    }
    if (!missingKeys.empty()) {
        FailureReport f;
        f.code = "F2002";
        f.message = "required field(s) missing: " + [&] {
            std::string s;
            for (size_t i = 0; i < missingKeys.size(); ++i)
                s += (i ? ", " : "") + missingKeys[i];
            return s;
        }();
        f.evidence = missingRefs;
        f.actions.push_back(
            {"re-capture the dump with all required identity fields",
             "missing identity data prevents reliable attribution", true});
        report.failures.push_back(std::move(f));
    }

    // Checksum correlation: integrity of the recovered bytes.
    auto expected = report.field("checksum_expected");
    auto actual = report.field("checksum_actual");
    std::vector<std::size_t> integritySteps{parseStep, normStep};
    if (expected && actual) {
        std::vector<EvidenceRef> refs{quoteLine(report, expected->sourceLine),
                                      quoteLine(report, actual->sourceLine)};
        std::size_t corrStep = log.add(
            PipelineStage::Correlate, "compare checksum pair",
            "expected=" + expected->value + " actual=" + actual->value, refs,
            refs);
        integritySteps.push_back(corrStep);
        if (lower(expected->value) == lower(actual->value)) {
            addFinding(report, "A-INTEGRITY-1", "dump byte integrity",
                       Verdict::AutoConfirmed,
                       "checksum matches: recovered dump is byte-identical",
                       "checksum_actual equals checksum_expected on the cited "
                       "lines",
                       refs, integritySteps);
        } else {
            addFinding(report, "A-INTEGRITY-1", "dump byte integrity",
                       Verdict::AutoRejected,
                       "checksum mismatch: dump bytes are corrupted",
                       "the two checksum values on the cited lines differ",
                       refs, integritySteps);
            FailureReport fail;
            fail.code = "F2004";
            fail.message = "checksum mismatch; evidence below is unreliable";
            fail.evidence = refs;
            fail.actions.push_back(
                {"recover the dump again from device crash storage",
                 "corrupted bytes cannot support an auto conclusion", true});
            fail.actions.push_back(
                {"verify the transport used to copy the dump off-device",
                 "a flaky copy path is a common source of bit errors", true});
            report.failures.push_back(std::move(fail));
        }
    }
    analyzeReplay(report, log, parseStep, normStep, exts);
}

void analyzeReplay(Report& report, StepLog& log, std::size_t parseStep,
                   std::size_t normStep,
                   const std::vector<ExtensionInfo>& exts) {
    auto replayStatus = report.field("replay_status");
    auto replayFrames = report.field("replay_frames_replayed");
    auto totalFrames = report.field("replay_frames_total");
    auto diverged = report.field("replay_diverged_frame");

    std::vector<EvidenceRef> replayRefs;
    for (const auto* maybe : {&replayStatus, &replayFrames, &totalFrames,
                              &diverged}) {
        if (*maybe) replayRefs.push_back(quoteLine(report, (*maybe)->sourceLine));
    }

    if (!replayStatus) {
        FailureReport f;
        f.code = "F2003";
        f.message = "no recovered replay record found";
        f.actions.push_back(
            {"run the recovery harness to produce replay_status and frame counts",
             "without a replay, the post-crash state cannot be cross-checked",
             true});
        f.actions.push_back(
            {"attach the original command-stream capture before re-running",
             "replay needs the captured command stream as input", true});
        report.failures.push_back(std::move(f));
        analyzeExtensions(report, log, parseStep, normStep, exts, {});
        return;
    }

    std::size_t classifyStep = log.add(
        PipelineStage::Classify, "classify recovered replay",
        "replay_status=" + replayStatus->value, replayRefs, replayRefs);

    std::size_t replayed = replayFrames ? std::stoull(replayFrames->value) : 0;
    std::size_t total = totalFrames ? std::stoull(totalFrames->value) : 0;
    bool hasDivergence = diverged && !diverged->value.empty() &&
                         lower(diverged->value) != "none";

    std::vector<std::size_t> steps{parseStep, normStep, classifyStep};
    if (lower(replayStatus->value) == "completed" && !hasDivergence &&
        total > 0 && replayed == total) {
        std::size_t corrStep = log.add(
            PipelineStage::Correlate, "correlate replay against frame range",
            std::to_string(replayed) + "/" + std::to_string(total) +
                " frames replayed, no divergence",
            replayRefs, replayRefs);
        steps.push_back(corrStep);
        addFinding(report, "A-REPLAY-1", "post-recovery replay",
                   Verdict::AutoConfirmed,
                   "replay completed and covers every captured frame",
                   "replay_status=completed, frame counts match and no "
                   "diverged frame is recorded",
                   replayRefs, steps);
    } else if (hasDivergence) {
        std::size_t corrStep = log.add(
            PipelineStage::Correlate, "pin first divergent frame",
            "diverged frame=" + diverged->value, replayRefs, replayRefs);
        steps.push_back(corrStep);
        addFinding(report, "A-REPLAY-1", "post-recovery replay",
                   Verdict::NeedsManualReview,
                   "replay diverges at frame " + diverged->value,
                   "an automatic pass cannot be claimed once frame output "
                   "diverges",
                   replayRefs, steps);
        ManualCheck mc;
        mc.item = "inspect frame " + diverged->value +
                  " against the pre-crash reference output";
        mc.reason = "replay reached the frame but its output diverged";
        mc.evidence = replayRefs;
        mc.actions.push_back(
            {"open the two framebuffers in the image diff tool",
             "visual diff decides whether the divergence is cosmetic or fatal",
             false});
        report.manualChecks.push_back(std::move(mc));
        if (report.manualBoundaryLine == 0)
            report.manualBoundaryLine = diverged->sourceLine;
    } else {
        addFinding(report, "A-REPLAY-1", "post-recovery replay",
                   Verdict::NeedsManualReview,
                   "replay is incomplete or frame counts are inconsistent",
                   "replay_status and frame counts do not satisfy the "
                   "auto-pass rule",
                   replayRefs, steps);
        ManualCheck mc;
        mc.item = "re-run replay and confirm frame coverage";
        mc.reason = "the recorded frame counts do not prove full coverage";
        mc.evidence = replayRefs;
        mc.actions.push_back(
            {"re-run the recovery harness with full frame logging",
             "complete frame logs are needed to confirm or reject coverage",
             false});
        report.manualChecks.push_back(std::move(mc));
        if (report.manualBoundaryLine == 0)
            report.manualBoundaryLine = replayStatus->sourceLine;
    }
    analyzeExtensions(report, log, parseStep, normStep, exts, steps);
}

void analyzeExtensions(Report& report, StepLog& log, std::size_t parseStep,
                       std::size_t normStep,
                       const std::vector<ExtensionInfo>& exts,
                       const std::vector<std::size_t>& replaySteps) {
    for (const ExtensionInfo& ext : exts) {
        EvidenceRef ref = quoteLine(report, ext.sourceLine);
        std::string name = normalizeName(ext.name);
        std::string status = normalizeStatus(ext.status);

        std::size_t classifyStep = log.add(
            PipelineStage::Classify, "classify extension " + name,
            "known=" +
                std::string(knownExtensions().count(name) ? "yes" : "no") +
                " status=" + (status.empty() ? "<absent>" : status),
            {ref}, {ref});
        std::vector<std::size_t> steps{parseStep, normStep, classifyStep};

        if (knownExtensions().count(name) == 0) {
            // Unknown extension: the single boundary case the pipeline cannot
            // decide automatically. Evidence must be retained verbatim.
            std::size_t corrStep = log.add(
                PipelineStage::Correlate,
                "cross-check unknown extension against replay",
                "no knowledge-base entry for " + name +
                    "; replay correlation cannot confirm or reject it",
                {ref}, {ref});
            steps.push_back(corrStep);
            addFinding(report, "A-EXT-UNKNOWN-" + name, name,
                       Verdict::Indeterminate,
                       "unknown extension: cannot judge crash relevance",
                       name + " is absent from the built-in knowledge base, so "
                              "the checker must not guess",
                       {ref}, steps);

            ManualCheck mc;
            mc.item = "manually confirm whether " + name +
                      " contributed to the crash";
            mc.reason = "unknown vendor/extension with no auto rule";
            mc.evidence = {ref};
            mc.actions.push_back(
                {"query the Vulkan device extension registry for " + name,
                 "the registry states the owner and the documented semantics",
                 false});
            mc.actions.push_back(
                {"reproduce the workload with " + name + " disabled if possible",
                 "a disable/re-enable A/B run gives empirical evidence", false});
            mc.actions.push_back(
                {"collect the vendor ISV ticket with this dump id attached",
                 "vendor-only extensions require vendor-side interpretation",
                 false});
            report.manualChecks.push_back(std::move(mc));
            if (report.manualBoundaryLine == 0)
                report.manualBoundaryLine = ext.sourceLine;
            continue;
        }

        if (status.empty()) {
            addFinding(report, "A-EXT-" + name, name,
                       Verdict::NeedsManualReview,
                       "known extension with no status token",
                       "the extension line omits status=... so support cannot "
                       "be evaluated",
                       {ref}, steps);
            continue;
        }
        addFinding(report, "A-EXT-" + name, name,
                   statusSupported(status) ? Verdict::AutoConfirmed
                                          : Verdict::AutoRejected,
                   "known extension marked " + status,
                   std::string("knowledge base recognizes the extension and "
                               "the normalized status token is ") +
                       status,
                   {ref}, steps);
    }

    // Stage 6: conclude, keeping replay steps visible for cross-reference.
    std::vector<EvidenceRef> boundaryRefs;
    if (report.manualBoundaryLine)
        boundaryRefs.push_back(quoteLine(report, report.manualBoundaryLine));
    log.add(PipelineStage::Conclude, "assemble provenance report",
            std::to_string(report.findings.size()) +
                " auto finding(s), " +
                std::to_string(report.manualChecks.size()) +
                " manual check(s), " +
                std::to_string(report.failures.size()) + " failure(s)",
            boundaryRefs, {});
    (void)replaySteps;
}

} // namespace

} // namespace gcdump
