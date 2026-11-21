#include "../src/core/analyzer.hpp"
#include "../src/core/parser.hpp"
#include "../src/core/render.hpp"

#include <cassert>
#include <sstream>

using namespace gcdump;

namespace {

Report analyzeText(const std::string& text) {
    std::istringstream in(text);
    return analyze(parseDump(in), "<memory>");
}

const Finding* findFinding(const Report& r, std::string_view idPrefix) {
    for (const auto& f : r.findings)
        if (f.id.rfind(idPrefix, 0) == 0) return &f;
    return nullptr;
}

void testLineProvenance() {
    Report r = analyzeText("dump_id: X\nfoo bar-no-colon\n");
    assert(r.hasErrors());
    assert(!r.failures.empty());
    bool anchored = false;
    for (const auto& e : r.failures.front().evidence)
        if (e.sourceLine == 2 && e.quote == "foo bar-no-colon") anchored = true;
    assert(anchored);
}

void testUnknownExtensionIsIndeterminate() {
    Report r = analyzeText(
        "dump_id: X\ndevice_name: d\ndriver_version: v\n"
        "checksum_expected: a\nchecksum_actual: a\n"
        "extension: VK_VENDORX_phantom_inference status=enabled\n"
        "replay_status: completed\nreplay_frames_total: 1\n"
        "replay_frames_replayed: 1\nreplay_diverged_frame: none\n");
    const Finding* f = findFinding(r, "A-EXT-UNKNOWN-VK_VENDORX");
    assert(f != nullptr);
    assert(f->verdict == Verdict::Indeterminate);
    assert(f->evidence.size() == 1 && f->evidence[0].sourceLine == 6);
    assert(r.manualBoundaryLine == 6);
    assert(!r.manualChecks.empty());
    assert(!r.manualChecks.front().actions.empty());
}

void testReplayDivergenceNeedsManual() {
    Report r = analyzeText(
        "dump_id: X\ndevice_name: d\ndriver_version: v\n"
        "checksum_expected: a\nchecksum_actual: a\n"
        "extension: VK_KHR_swapchain status=enabled\n"
        "replay_status: completed\nreplay_frames_total: 8\n"
        "replay_frames_replayed: 8\nreplay_diverged_frame: 3\n");
    const Finding* f = findFinding(r, "A-REPLAY-1");
    assert(f && f->verdict == Verdict::NeedsManualReview);
    assert(r.manualBoundaryLine == 10);
}

void testCleanReplayAndChecksumConfirmed() {
    Report r = analyzeText(
        "dump_id: X\ndevice_name: d\ndriver_version: v\n"
        "checksum_expected: aa\nchecksum_actual: aa\n"
        "extension: VK_KHR_surface status=enabled\n"
        "replay_status: completed\nreplay_frames_total: 4\n"
        "replay_frames_replayed: 4\nreplay_diverged_frame: none\n");
    const Finding* integrity = findFinding(r, "A-INTEGRITY-1");
    const Finding* replay = findFinding(r, "A-REPLAY-1");
    assert(integrity && integrity->verdict == Verdict::AutoConfirmed);
    assert(replay && replay->verdict == Verdict::AutoConfirmed);
    assert(r.failures.empty());
    assert(!renderReport(r, ReportFormat::Markdown).empty());
}

void testChecksumMismatchFailure() {
    Report r = analyzeText(
        "dump_id: X\ndevice_name: d\ndriver_version: v\n"
        "checksum_expected: aa\nchecksum_actual: bb\n"
        "replay_status: completed\nreplay_frames_total: 1\n"
        "replay_frames_replayed: 1\nreplay_diverged_frame: none\n");
    bool hasF2004 = false;
    for (const auto& f : r.failures)
        if (f.code == "F2004" && !f.actions.empty()) hasF2004 = true;
    assert(hasF2004);
}

} // namespace

int main() {
    testLineProvenance();
    testUnknownExtensionIsIndeterminate();
    testReplayDivergenceNeedsManual();
    testCleanReplayAndChecksumConfirmed();
    testChecksumMismatchFailure();
    return 0;
}
