#include "render.hpp"

#include <algorithm>
#include <sstream>

namespace gcdump {

namespace {

void appendEvidence(std::ostringstream& out, const std::vector<EvidenceRef>& refs,
                    bool markdown) {
    for (const auto& e : refs) {
        if (markdown)
            out << "    - evidence L" << e.sourceLine << ": `" << e.quote
                << "`\n";
        else
            out << "    evidence L" << e.sourceLine << ": " << e.quote << "\n";
    }
}

void appendSteps(std::ostringstream& out, const Report& report,
                 const std::vector<std::size_t>& steps, bool markdown) {
    for (std::size_t order : steps) {
        auto it = std::find_if(report.steps.begin(), report.steps.end(),
                               [order](const StepRecord& s) {
                                   return s.order == order;
                               });
        if (it == report.steps.end()) continue;
        out << (markdown ? "    - step #" : "    step #") << it->order << " ["
            << to_string(it->stage) << "] " << it->action << " — "
            << it->detail << "\n";
    }
}

} // namespace

std::string renderSummaryLine(const Report& report) {
    std::size_t confirmed = 0, rejected = 0, indeterminate = 0, manual = 0;
    for (const auto& f : report.findings) {
        switch (f.verdict) {
        case Verdict::AutoConfirmed: ++confirmed; break;
        case Verdict::AutoRejected: ++rejected; break;
        case Verdict::Indeterminate: ++indeterminate; break;
        case Verdict::NeedsManualReview: ++manual; break;
        }
    }
    std::ostringstream out;
    out << "findings=" << report.findings.size()
        << " confirmed=" << confirmed << " rejected=" << rejected
        << " indeterminate=" << indeterminate
        << " manual_review=" << manual
        << " failures=" << report.failures.size();
    return out.str();
}

std::string renderReport(const Report& report, ReportFormat format) {
    const bool md = format == ReportFormat::Markdown;
    std::ostringstream out;
    auto h = [&](int level, const std::string& title) {
        if (md) {
            out << std::string(level, '#') << ' ' << title << "\n\n";
        } else {
            out << "=== " << title << " ===\n";
        }
    };

    h(1, "GPU crash-dump provenance report");
    out << (md ? "- " : "  ") << "dump id: "
        << (report.dumpId.empty() ? "<unknown>" : report.dumpId) << "\n";
    out << (md ? "- " : "  ") << "source: "
        << (report.sourcePath.empty() ? "<stdin>" : report.sourcePath) << "\n";
    out << (md ? "- " : "  ") << renderSummaryLine(report) << "\n\n";

    h(2, "Processing steps (provenance chain)");
    for (const auto& s : report.steps) {
        out << (md ? "- " : "  ") << "#" << s.order << " ["
            << to_string(s.stage) << "] " << s.action << " — " << s.detail
            << "\n";
        for (const auto& e : s.inputs)
            out << "    in  L" << e.sourceLine << ": " << e.quote << "\n";
        for (const auto& e : s.outputs)
            out << "    out L" << e.sourceLine << ": " << e.quote << "\n";
    }
    out << "\n";

    h(2, "Automatic conclusions");
    if (report.findings.empty()) {
        out << (md ? "_" : "") << "none emitted"
            << (md ? "_" : "") << "\n\n";
    }
    for (const auto& f : report.findings) {
        out << (md ? "### " : "  * ") << "[" << to_string(f.verdict) << "] "
            << f.id << " — " << f.subject << "\n";
        out << "    summary: " << f.summary << "\n";
        out << "    rationale: " << f.rationale << "\n";
        appendEvidence(out, f.evidence, md);
        appendSteps(out, report, f.steps, md);
        out << "\n";
    }

    h(2, "Manual confirmation required");
    if (report.manualChecks.empty()) {
        out << (md ? "_" : "") << "none" << (md ? "_" : "") << "\n\n";
    }
    for (const auto& mc : report.manualChecks) {
        out << (md ? "### " : "  * ") << mc.item << "\n";
        out << "    reason: " << mc.reason << "\n";
        appendEvidence(out, mc.evidence, md);
        for (const auto& a : mc.actions)
            out << "    action: " << a.action << " (why: " << a.rationale
                << ")\n";
        out << "\n";
    }

    h(2, "Failures and executable follow-up");
    if (report.failures.empty()) {
        out << (md ? "_" : "") << "no blocking failures"
            << (md ? "_" : "") << "\n\n";
    }
    for (const auto& fail : report.failures) {
        out << (md ? "### " : "  * ") << fail.code << ": " << fail.message
            << "\n";
        appendEvidence(out, fail.evidence, md);
        for (const auto& a : fail.actions)
            out << "    " << (a.blocksReliability ? "[BLOCKING] " : "")
                << "action: " << a.action << " (why: " << a.rationale
                << ")\n";
        out << "\n";
    }

    if (report.manualBoundaryLine) {
        h(2, "Retained boundary evidence");
        const SourceLine* l = report.findLine(report.manualBoundaryLine);
        out << "the pipeline could not decide automatically; the original "
               "line is retained untouched:\n";
        out << "  L" << report.manualBoundaryLine << ": "
            << (l ? l->raw : "<missing>") << "\n\n";
    }
    return out.str();
}

} // namespace gcdump
