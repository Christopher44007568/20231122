#include "experiment.hpp"

#include "png_writer.hpp"
#include "record.hpp"
#include "sha256.hpp"
#include "state.hpp"
#include "vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace offscreen {
namespace {

namespace fs = std::filesystem;

struct StepRecord {
    std::string run;
    std::uint32_t traceStep = 0;
    std::uint32_t logicalStep = 0;
    std::string kind;
    std::string inputPath;
    Event event;
    State before;
    State after;
    StepOutcome outcome;
    std::string frameSha256;
    std::string framePng;
    bool repeatCaptureMatch = true;
    Bytes rgba;
};

struct Comparison {
    std::string run;
    std::uint32_t traceStep = 0;
    std::uint32_t logicalStep = 0;
    std::int64_t actualEventId = -1;
    std::int64_t expectedEventId = -1;
    bool eventOrderMatch = false;
    bool stateMatch = false;
    bool frameMatch = false;
    bool protocolMatch = false;
    bool repeatCaptureMatch = true;
    std::string actualStateSha256;
    std::string expectedStateSha256;
    std::string actualFrameSha256;
    std::string expectedFrameSha256;
    std::string actualPng;
    std::string expectedPng;
    std::uint64_t differingPixels = 0;
    std::uint32_t maxChannelDelta = 0;
    std::int64_t firstDiffX = -1;
    std::int64_t firstDiffY = -1;
    Bytes actualRgba;
    Bytes expectedRgba;
};

struct RunSummary {
    std::string name;
    std::string expectation;
    std::size_t steps = 0;
    std::size_t stateMismatches = 0;
    std::size_t frameMismatches = 0;
    std::size_t orderMismatches = 0;
    std::size_t repeatFailures = 0;
    bool expectationMet = false;
};

std::string stepJson(const StepRecord& step) {
    std::ostringstream out;
    out << "{\"run\":\"" << jsonEscape(step.run) << "\",\"trace_step\":"
        << step.traceStep << ",\"logical_step\":" << step.logicalStep
        << ",\"kind\":\"" << step.kind << "\",\"input\":{";
    if (step.kind == "restore") {
        out << "\"checkpoint\":\"" << jsonEscape(step.inputPath) << "\"";
    } else {
        out << "\"event_id\":" << step.event.id << ",\"raw\":\""
            << jsonEscape(step.event.raw) << "\",\"op\":\""
            << jsonEscape(step.event.op) << "\",\"arg0\":\""
            << jsonEscape(step.event.arg0) << "\",\"arg1\":\""
            << jsonEscape(step.event.arg1) << "\"";
    }
    out << "},\"before_visual_sha256\":\"" << step.before.visualHash()
        << "\",\"result\":{\"status\":\"" << jsonEscape(step.outcome.status)
        << "\",\"detail\":\"" << jsonEscape(step.outcome.detail)
        << "\",\"visual_changed\":" << (step.outcome.visualChanged ? "true" : "false")
        << "},\"state\":" << step.after.json() << ",\"state_sha256\":\""
        << step.after.visualHash() << "\",\"protocol_sha256\":\""
        << sha256Hex(step.after.protocol.json()) << "\",\"frame_sha256\":\""
        << step.frameSha256 << "\",\"repeat_capture_match\":"
        << (step.repeatCaptureMatch ? "true" : "false") << ",\"frame_png\":\""
        << jsonEscape(step.framePng) << "\"}\n";
    return out.str();
}

std::string comparisonJson(const Comparison& comparison) {
    std::ostringstream out;
    out << "{\"run\":\"" << jsonEscape(comparison.run) << "\",\"trace_step\":"
        << comparison.traceStep << ",\"logical_step\":" << comparison.logicalStep
        << ",\"actual_event_id\":" << comparison.actualEventId
        << ",\"expected_event_id\":" << comparison.expectedEventId
        << ",\"event_order_match\":"
        << (comparison.eventOrderMatch ? "true" : "false")
        << ",\"state_match\":" << (comparison.stateMatch ? "true" : "false")
        << ",\"frame_match\":" << (comparison.frameMatch ? "true" : "false")
        << ",\"protocol_match\":" << (comparison.protocolMatch ? "true" : "false")
        << ",\"repeat_capture_match\":"
        << (comparison.repeatCaptureMatch ? "true" : "false")
        << ",\"actual_state_sha256\":\"" << comparison.actualStateSha256
        << "\",\"expected_state_sha256\":\"" << comparison.expectedStateSha256
        << "\",\"actual_frame_sha256\":\"" << comparison.actualFrameSha256
        << "\",\"expected_frame_sha256\":\"" << comparison.expectedFrameSha256
        << "\",\"actual_png\":\"" << jsonEscape(comparison.actualPng)
        << "\",\"expected_png\":\"" << jsonEscape(comparison.expectedPng)
        << "\",\"differing_pixels\":" << comparison.differingPixels
        << ",\"max_channel_delta\":" << comparison.maxChannelDelta
        << ",\"first_diff_x\":" << comparison.firstDiffX
        << ",\"first_diff_y\":" << comparison.firstDiffY << "}\n";
    return out.str();
}

std::vector<Event> tailFrom(const std::vector<Event>& events, std::size_t first) {
    if (first >= events.size()) return {};
    return std::vector<Event>(events.begin() + static_cast<std::ptrdiff_t>(first),
                              events.end());
}

std::string framePath(const std::string& run, std::uint32_t traceStep) {
    std::ostringstream out;
    out << "runs/" << run << "/frames/step-" << std::setw(3) << std::setfill('0')
        << traceStep << ".png";
    return out.str();
}

class Experiment {
public:
    explicit Experiment(const ExperimentOptions& options);
    int run();

private:
    StepRecord captureStep(const std::string& run, std::uint32_t traceStep,
                           std::uint32_t logicalStep, const std::string& kind,
                           const std::string& inputPath, const Event& event,
                           const State& before, const State& after,
                           const StepOutcome& outcome);
    std::vector<StepRecord> runFromCheckpoint(
        const std::string& run, const Checkpoint& checkpoint,
        const std::vector<Event>& events, const std::vector<std::uint32_t>& refs,
        const std::vector<bool>& checkOrder = {});
    void compare(const StepRecord& actual, const StepRecord& expected,
                 bool countOrderMismatch = true);
    void writeTrace(const std::string& run, const std::vector<StepRecord>& steps);
    void writeReports();
    void writeManifest();
    std::string summaryJson() const;
    std::string summaryMarkdown() const;
    const Comparison* firstError() const;

    ExperimentOptions options_;
    VulkanRenderer renderer_;
    fs::path recordPath_;
    fs::path output_;
    Record sourceRecord_;
    std::vector<Event> events_;
    Record truncatedRecord_;
    std::vector<StepRecord> baseline_;
    std::vector<StepRecord> truncatedPrefix_;
    std::vector<StepRecord> recovery_;
    std::vector<Comparison> comparisons_;
    std::vector<RunSummary> summaries_;
    std::vector<std::string> expectationViolations_;
    Checkpoint checkpoint_;
};

}  // namespace

Experiment::Experiment(const ExperimentOptions& options)
    : options_(options),
      renderer_({options.width, options.height}),
      recordPath_(options.recordPath),
      output_(options.outputDirectory),
      sourceRecord_(loadRecord(options.recordPath)),
      events_(sourceRecord_.events) {
    if (events_.size() < 7) {
        throw std::runtime_error("record needs at least seven events");
    }
    if (sourceRecord_.truncatedTail) {
        throw std::runtime_error("canonical input record itself is truncated");
    }
    if (fs::exists(output_)) fs::remove_all(output_);
    ensureDirectory(output_ / "inputs");
    ensureDirectory(output_ / "runs");
}

StepRecord Experiment::captureStep(const std::string& run,
                                   std::uint32_t traceStep,
                                   std::uint32_t logicalStep,
                                   const std::string& kind,
                                   const std::string& inputPath,
                                   const Event& event, const State& before,
                                   const State& after,
                                   const StepOutcome& outcome) {
    StepRecord step;
    step.run = run;
    step.traceStep = traceStep;
    step.logicalStep = logicalStep;
    step.kind = kind;
    step.inputPath = inputPath;
    step.event = event;
    step.before = before;
    step.after = after;
    step.outcome = outcome;

    const Capture first = renderer_.capture(after.visual);
    if (options_.doubleCapture) {
        const Capture second = renderer_.capture(after.visual);
        step.repeatCaptureMatch = first.sha256 == second.sha256;
    }
    step.framePng = framePath(run, traceStep);
    writeRgbaPng(output_ / step.framePng, first.width, first.height, first.rgba);
    step.frameSha256 = first.sha256;
    step.rgba = first.rgba;
    return step;
}

void Experiment::compare(const StepRecord& actual, const StepRecord& expected,
                         bool countOrderMismatch) {
    Comparison comparison;
    comparison.run = actual.run;
    comparison.traceStep = actual.traceStep;
    comparison.logicalStep = actual.logicalStep;
    comparison.actualEventId = actual.kind == "restore"
                                   ? -1
                                   : static_cast<std::int64_t>(actual.event.id);
    comparison.expectedEventId =
        static_cast<std::int64_t>(expected.event.id);
    comparison.eventOrderMatch =
        !countOrderMismatch || actual.kind == "restore" ||
        actual.event.id == expected.event.id;
    comparison.stateMatch = actual.after.visualHash() == expected.after.visualHash();
    comparison.frameMatch = actual.frameSha256 == expected.frameSha256;
    comparison.protocolMatch =
        sha256Hex(actual.after.protocol.json()) ==
        sha256Hex(expected.after.protocol.json());
    comparison.repeatCaptureMatch = actual.repeatCaptureMatch;
    comparison.actualStateSha256 = actual.after.visualHash();
    comparison.expectedStateSha256 = expected.after.visualHash();
    comparison.actualFrameSha256 = actual.frameSha256;
    comparison.expectedFrameSha256 = expected.frameSha256;
    comparison.actualPng = actual.framePng;
    comparison.expectedPng = expected.framePng;
    if (actual.rgba.size() == expected.rgba.size()) {
        for (std::size_t pixel = 0; pixel < actual.rgba.size() / 4; ++pixel) {
            bool differs = false;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const std::uint8_t a = actual.rgba[pixel * 4 + channel];
                const std::uint8_t b = expected.rgba[pixel * 4 + channel];
                const std::uint32_t delta = a > b ? a - b : b - a;
                comparison.maxChannelDelta =
                    std::max(comparison.maxChannelDelta, delta);
                if (delta != 0) differs = true;
            }
            if (differs) {
                if (comparison.differingPixels == 0) {
                    comparison.firstDiffX =
                        static_cast<std::int64_t>(pixel % options_.width);
                    comparison.firstDiffY =
                        static_cast<std::int64_t>(pixel / options_.width);
                }
                ++comparison.differingPixels;
            }
        }
        if (!comparison.stateMatch || !comparison.frameMatch) {
            comparison.actualRgba = actual.rgba;
            comparison.expectedRgba = expected.rgba;
        }
    }
    comparisons_.push_back(comparison);
}

std::vector<StepRecord> Experiment::runFromCheckpoint(
    const std::string& run, const Checkpoint& checkpoint,
    const std::vector<Event>& events,
    const std::vector<std::uint32_t>& refs,
    const std::vector<bool>& checkOrder) {
    if (events.size() != refs.size()) {
        throw std::runtime_error("branch events and baseline refs differ in size");
    }
    if (!checkOrder.empty() && checkOrder.size() != events.size()) {
        throw std::runtime_error("branch order-check mask differs in size");
    }
    if (baseline_.size() <= 4) {
        throw std::runtime_error("baseline does not contain checkpoint state");
    }

    std::vector<StepRecord> trace;
    StateMachine machine = checkpoint.machine;
    StepOutcome restoreOutcome;
    restoreOutcome.status = "checkpoint_restored";
    restoreOutcome.detail = "restored checkpoint state and verified its saved visual hash";
    restoreOutcome.visualChanged = false;
    const State restored = machine.snapshot();
    StepRecord restore =
        captureStep(run, 0, 4, "restore", "inputs/recovery-checkpoint.ckpt", {},
                    restored, restored, restoreOutcome);
    compare(restore, baseline_[4], false);
    trace.push_back(restore);

    for (std::size_t i = 0; i < events.size(); ++i) {
        const State before = machine.snapshot();
        const StepOutcome outcome = machine.apply(events[i]);
        const State after = machine.snapshot();
        StepRecord step =
            captureStep(run, static_cast<std::uint32_t>(i + 1), refs[i], "event",
                        "", events[i], before, after, outcome);
        const bool orderMatters = checkOrder.empty() || checkOrder[i];
        compare(step, baseline_[refs[i]], orderMatters);
        trace.push_back(step);
    }
    return trace;
}

void Experiment::writeTrace(const std::string& run,
                            const std::vector<StepRecord>& steps) {
    const fs::path path = output_ / "runs" / run / "trace.jsonl";
    ensureDirectory(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot write " + path.string());
    for (const StepRecord& step : steps) output << stepJson(step);
}

int Experiment::run() {
    writeRecord(output_ / "inputs" / "canonical.record", events_);

    StateMachine machine;
    for (std::size_t i = 0; i < events_.size(); ++i) {
        const State before = machine.snapshot();
        const StepOutcome outcome = machine.apply(events_[i]);
        baseline_.push_back(captureStep(
            "baseline", static_cast<std::uint32_t>(i),
            static_cast<std::uint32_t>(i), "event", "", events_[i], before,
            machine.snapshot(), outcome));
    }
    writeTrace("baseline", baseline_);
    if (baseline_.size() < 12) {
        throw std::runtime_error("record must contain at least twelve events");
    }

    const std::string truncatedText = makeTruncatedRecord(events_, 4, 13);
    writeText(output_ / "inputs" / "truncated.record", truncatedText);
    truncatedRecord_ = loadRecord(output_ / "inputs" / "truncated.record");
    if (!truncatedRecord_.truncatedTail || truncatedRecord_.events.size() != 5) {
        throw std::runtime_error("truncated record did not preserve the expected prefix");
    }

    StateMachine prefixMachine;
    for (std::size_t i = 0; i < truncatedRecord_.events.size(); ++i) {
        const State before = prefixMachine.snapshot();
        const StepOutcome outcome = prefixMachine.apply(truncatedRecord_.events[i]);
        StepRecord step = captureStep(
            "truncated-prefix", static_cast<std::uint32_t>(i),
            static_cast<std::uint32_t>(i), "event", "",
            truncatedRecord_.events[i], before, prefixMachine.snapshot(), outcome);
        compare(step, baseline_[i]);
        truncatedPrefix_.push_back(step);
    }
    writeTrace("truncated-prefix", truncatedPrefix_);
    if (truncatedPrefix_.back().after.visualHash() != baseline_[4].after.visualHash() ||
        truncatedPrefix_.back().frameSha256 != baseline_[4].frameSha256) {
        throw std::runtime_error("truncated prefix replay diverged from baseline");
    }

    const std::string checkpointText = serializeCheckpoint(
        truncatedPrefix_.back().after, truncatedPrefix_.back().frameSha256);
    writeText(output_ / "inputs" / "recovery-checkpoint.ckpt", checkpointText);
    checkpoint_ = loadCheckpoint(output_ / "inputs" / "recovery-checkpoint.ckpt");
    if (checkpoint_.machine.snapshot().visualHash() !=
            baseline_[4].after.visualHash() ||
        checkpoint_.frameSha256 != baseline_[4].frameSha256) {
        throw std::runtime_error("checkpoint round-trip verification failed");
    }
    writeText(output_ / "inputs" / "recovery-checkpoint.json",
              "{\"state\":" + baseline_[4].after.json() +
                  ",\"frame_sha256\":\"" + baseline_[4].frameSha256 + "\"}\n");

    std::vector<Event> recoveryTail = tailFrom(events_, 5);
    writeRecord(output_ / "inputs" / "recovery-tail.record", recoveryTail);
    recoveryTail =
        loadRecord(output_ / "inputs" / "recovery-tail.record").events;

    std::vector<std::uint32_t> recoveryRefs;
    for (std::uint32_t i = 5; i < 12; ++i) recoveryRefs.push_back(i);
    recovery_ = runFromCheckpoint("recovery", checkpoint_, recoveryTail,
                                  recoveryRefs);
    writeTrace("recovery", recovery_);

    std::vector<Event> reordered = recoveryTail;
    std::swap(reordered[3], reordered[4]);
    writeRecord(output_ / "inputs" / "reordered-tail.record", reordered);
    reordered = loadRecord(output_ / "inputs" / "reordered-tail.record").events;
    const std::vector<StepRecord> reorder =
        runFromCheckpoint("reorder", checkpoint_, reordered, recoveryRefs);
    writeTrace("reorder", reorder);

    std::vector<Event> duplicateEvent = recoveryTail;
    duplicateEvent.push_back(recoveryTail.back());
    writeRecord(output_ / "inputs" / "duplicate-event-tail.record", duplicateEvent);
    duplicateEvent =
        loadRecord(output_ / "inputs" / "duplicate-event-tail.record").events;
    std::vector<std::uint32_t> duplicateRefs = recoveryRefs;
    duplicateRefs.push_back(11);
    const std::vector<StepRecord> duplicateEventSteps =
        runFromCheckpoint("duplicate-event", checkpoint_, duplicateEvent,
                          duplicateRefs);
    writeTrace("duplicate-event", duplicateEventSteps);

    std::vector<Event> duplicateSubmit = recoveryTail;
    Event repeatedSubmission;
    repeatedSubmission.id = 12;
    repeatedSubmission.op = "SUBMIT";
    repeatedSubmission.arg0 = "rev-2";
    duplicateSubmit.push_back(repeatedSubmission);
    writeRecord(output_ / "inputs" / "duplicate-submit-tail.record",
                duplicateSubmit);
    duplicateSubmit =
        loadRecord(output_ / "inputs" / "duplicate-submit-tail.record").events;
    const std::vector<StepRecord> duplicateSubmitSteps =
        runFromCheckpoint("duplicate-submit", checkpoint_, duplicateSubmit,
                          duplicateRefs,
                          {true, true, true, true, true, true, true, false});
    writeTrace("duplicate-submit", duplicateSubmitSteps);

    std::size_t baselineRepeatFailures = 0;
    for (const StepRecord& step : baseline_) {
        if (!step.repeatCaptureMatch) ++baselineRepeatFailures;
    }
    summaries_.push_back({"baseline", "exact repeat capture", baseline_.size(), 0, 0,
                          0, baselineRepeatFailures, baselineRepeatFailures == 0});

    const auto summarize = [&](const std::string& run, const std::string& expectation,
                               const std::vector<StepRecord>& steps) {
        RunSummary summary;
        summary.name = run;
        summary.expectation = expectation;
        summary.steps = steps.size();
        for (const Comparison& comparison : comparisons_) {
            if (comparison.run != run) continue;
            if (!comparison.stateMatch) ++summary.stateMismatches;
            if (!comparison.frameMatch) ++summary.frameMismatches;
            if (!comparison.eventOrderMatch) ++summary.orderMismatches;
            if (!comparison.repeatCaptureMatch) ++summary.repeatFailures;
        }
        return summary;
    };

    RunSummary prefixSummary =
        summarize("truncated-prefix",
                  "truncated prefix replay matches baseline steps 0-4",
                  truncatedPrefix_);
    prefixSummary.expectationMet = prefixSummary.stateMismatches == 0 &&
                                   prefixSummary.frameMismatches == 0 &&
                                   prefixSummary.orderMismatches == 0 &&
                                   prefixSummary.repeatFailures == 0;
    summaries_.push_back(prefixSummary);

    RunSummary recoverySummary =
        summarize("recovery", "all restored and replayed states match baseline",
                  recovery_);
    recoverySummary.expectationMet = recoverySummary.stateMismatches == 0 &&
                                     recoverySummary.frameMismatches == 0 &&
                                     recoverySummary.orderMismatches == 0 &&
                                     recoverySummary.repeatFailures == 0;
    summaries_.push_back(recoverySummary);

    RunSummary reorderSummary =
        summarize("reorder", "first visual divergence is at logical step 8",
                  reorder);
    const auto reorderError = std::find_if(
        comparisons_.begin(), comparisons_.end(), [](const Comparison& value) {
            return value.run == "reorder" &&
                   (!value.stateMatch || !value.frameMatch);
        });
    reorderSummary.expectationMet =
        reorderError != comparisons_.end() && reorderError->logicalStep == 8;
    summaries_.push_back(reorderSummary);

    RunSummary duplicateEventSummary =
        summarize("duplicate-event", "exact duplicate event is dropped",
                  duplicateEventSteps);
    const bool duplicateEventStatus =
        duplicateEventSteps.back().outcome.status == "duplicate_event_dropped";
    duplicateEventSummary.expectationMet =
        duplicateEventStatus && duplicateEventSummary.stateMismatches == 0 &&
        duplicateEventSummary.frameMismatches == 0 &&
        duplicateEventSummary.repeatFailures == 0;
    summaries_.push_back(duplicateEventSummary);

    RunSummary duplicateSubmitSummary =
        summarize("duplicate-submit", "repeated submission token is idempotent",
                  duplicateSubmitSteps);
    const bool duplicateSubmitStatus =
        duplicateSubmitSteps.back().outcome.status == "duplicate_submit_deduped";
    duplicateSubmitSummary.expectationMet =
        duplicateSubmitStatus && duplicateSubmitSummary.stateMismatches == 0 &&
        duplicateSubmitSummary.frameMismatches == 0 &&
        duplicateSubmitSummary.repeatFailures == 0;
    summaries_.push_back(duplicateSubmitSummary);

    for (const RunSummary& summary : summaries_) {
        if (!summary.expectationMet) {
            expectationViolations_.push_back(
                summary.name + ": expected " + summary.expectation);
        }
    }

    writeReports();
    writeManifest();

    const Comparison* error = firstError();
    std::cout << "device: " << renderer_.deviceName() << " | driver "
              << renderer_.driverName() << " | " << renderer_.vulkanFormat() << "\n";
    std::cout << "record: " << sourceRecord_.sha256.substr(0, 16) << " events="
              << events_.size() << " truncated_bytes="
              << truncatedRecord_.byteCount << " ignored_tail_bytes="
              << truncatedRecord_.incompleteTail.size() << "\n";
    std::cout << "recovery: " << recoverySummary.stateMismatches
              << " state mismatches, " << recoverySummary.frameMismatches
              << " frame mismatches\n";
    if (error != nullptr) {
        std::cout << "first error state: run=" << error->run
                  << " logical_step=" << error->logicalStep
                  << " actual_event=" << error->actualEventId
                  << " expected_event=" << error->expectedEventId << "\n";
        std::cout << "  expected frame " << error->expectedFrameSha256 << "\n"
                  << "  actual   frame " << error->actualFrameSha256 << "\n";
    } else {
        std::cout << "first error state: none\n";
    }
    if (!expectationViolations_.empty()) {
        for (const std::string& violation : expectationViolations_) {
            std::cerr << "UNEXPECTED: " << violation << '\n';
        }
        return 2;
    }
    std::cout << "report: " << (output_ / "report.md") << "\n";
    return 0;
}

const Comparison* Experiment::firstError() const {
    for (const Comparison& comparison : comparisons_) {
        if (!comparison.stateMatch || !comparison.frameMatch ||
            !comparison.repeatCaptureMatch) {
            return &comparison;
        }
    }
    return nullptr;
}

std::string Experiment::summaryJson() const {
    const Comparison* error = firstError();
    std::ostringstream out;
    out << "{\n"
        << "  \"experiment\": \"offscreen-screenshot-consistency\",\n"
        << "  \"generated_at\": \"" << utcNow() << "\",\n"
        << "  \"technical\": {\"language\": \"C++17\", \"graphics_api\": "
           "\"Vulkan\", \"window_api\": \"GLFW\"},\n"
        << "  \"renderer\": {\n"
        << "    \"device\": \"" << jsonEscape(renderer_.deviceName()) << "\",\n"
        << "    \"driver\": \"" << jsonEscape(renderer_.driverName()) << "\",\n"
        << "    \"api_version\": \"" << jsonEscape(renderer_.apiVersion())
        << "\",\n"
        << "    \"glfw_platform\": \"" << jsonEscape(renderer_.glfwPlatform())
        << "\",\n"
        << "    \"format\": \"" << jsonEscape(renderer_.vulkanFormat())
        << "\",\n"
        << "    \"width\": " << options_.width << ",\n"
        << "    \"height\": " << options_.height << ",\n"
        << "    \"double_capture\": "
        << (options_.doubleCapture ? "true" : "false") << "\n"
        << "  },\n"
        << "  \"record\": {\"path\": \"" << jsonEscape(sourceRecord_.path.string())
        << "\", \"sha256\": \"" << sourceRecord_.sha256 << "\", \"bytes\": "
        << sourceRecord_.byteCount << ", \"event_count\": " << events_.size()
        << "},\n"
        << "  \"truncation\": {\"path\": \"inputs/truncated.record\", \"bytes\": "
        << truncatedRecord_.byteCount << ", \"complete_events\": "
        << truncatedRecord_.events.size() << ", \"ignored_tail_bytes\": "
        << truncatedRecord_.incompleteTail.size() << ", \"incomplete_tail\": \""
        << jsonEscape(truncatedRecord_.incompleteTail) << "\"},\n"
        << "  \"checkpoint\": {\"path\": \"inputs/recovery-checkpoint.ckpt\", "
           "\"logical_step\": 4, \"frame_sha256\": \""
        << checkpoint_.frameSha256 << "\"},\n"
        << "  \"runs\": [\n";
    for (std::size_t i = 0; i < summaries_.size(); ++i) {
        const RunSummary& summary = summaries_[i];
        out << "    {\"name\": \"" << jsonEscape(summary.name)
            << "\", \"expectation\": \"" << jsonEscape(summary.expectation)
            << "\", \"steps\": " << summary.steps
            << ", \"state_mismatches\": " << summary.stateMismatches
            << ", \"frame_mismatches\": " << summary.frameMismatches
            << ", \"event_order_mismatches\": " << summary.orderMismatches
            << ", \"repeat_capture_failures\": " << summary.repeatFailures
            << ", \"expectation_met\": "
            << (summary.expectationMet ? "true" : "false") << "}"
            << (i + 1 == summaries_.size() ? "" : ",") << "\n";
    }
    out << "  ],\n  \"first_error_state\": ";
    if (error == nullptr) {
        out << "null";
    } else {
        out << "{\"run\": \"" << jsonEscape(error->run)
            << "\", \"logical_step\": " << error->logicalStep
            << ", \"trace_step\": " << error->traceStep
            << ", \"actual_event_id\": " << error->actualEventId
            << ", \"expected_event_id\": " << error->expectedEventId
            << ", \"state_match\": " << (error->stateMatch ? "true" : "false")
            << ", \"frame_match\": " << (error->frameMatch ? "true" : "false")
            << ", \"actual_state_sha256\": \"" << error->actualStateSha256
            << "\", \"expected_state_sha256\": \"" << error->expectedStateSha256
            << "\", \"actual_frame_sha256\": \"" << error->actualFrameSha256
            << "\", \"expected_frame_sha256\": \"" << error->expectedFrameSha256
            << "\", \"actual_png\": \"" << jsonEscape(error->actualPng)
            << "\", \"expected_png\": \"" << jsonEscape(error->expectedPng)
            << "\", \"differing_pixels\": " << error->differingPixels
            << ", \"max_channel_delta\": " << error->maxChannelDelta
            << ", \"first_diff_x\": " << error->firstDiffX
            << ", \"first_diff_y\": " << error->firstDiffY
            << "}";
    }
    out << ",\n  \"expectation_violations\": [";
    for (std::size_t i = 0; i < expectationViolations_.size(); ++i) {
        if (i != 0) out << ", ";
        out << '"' << jsonEscape(expectationViolations_[i]) << '"';
    }
    out << "],\n  \"verdict\": \""
        << (expectationViolations_.empty()
                ? "all expected controls observed; first screenshot-state error located"
                : "unexpected experiment outcome")
        << "\"\n}\n";
    return out.str();
}

std::string Experiment::summaryMarkdown() const {
    const Comparison* error = firstError();
    std::ostringstream out;
    out << "# 离屏截图一致性实验\n\n"
        << "- 生成时间：`" << utcNow() << "`\n"
        << "- 技术栈：C++17 / Vulkan / GLFW\n"
        << "- GPU：`" << renderer_.deviceName() << "`，驱动 `"
        << renderer_.driverName() << "`，格式 `" << renderer_.vulkanFormat()
        << "`，分辨率 `" << options_.width << "x" << options_.height << "`\n"
        << "- GLFW 平台：`" << renderer_.glfwPlatform()
        << "`；离屏渲染不读取窗口像素\n"
        << "- 基准记录：`" << sourceRecord_.sha256
        << "`，共 " << events_.size() << " 条事件\n\n";

    out << "## 截断与恢复\n\n"
        << "`inputs/truncated.record` 只保留 0-4 号完整事件，随后是未换行的残缺前缀 "
           "`"
        << jsonEscape(truncatedRecord_.incompleteTail) << "`。加载器识别出 "
        << truncatedRecord_.incompleteTail.size()
        << " 字节尾部并拒绝将其当作事件。这 5 条完整事件先独立重放，逐步匹配基准 "
           "0-4 号状态；轨迹见 `runs/truncated-prefix/trace.jsonl`。检查点来自该前缀"
           "重放的末态（`inputs/recovery-checkpoint.ckpt`），恢复分支校验视觉、协议"
           "和帧哈希后，"
           "再从该状态继续消费 5-11 号事件。\n\n";

    out << "## 逐步结果\n\n"
        << "| 分支 | 预期 | 步骤 | 状态差异 | 帧差异 | 事件顺序差异 | 二次截图差异 | 符合预期 |\n"
        << "|---|---|---:|---:|---:|---:|---:|---|\n";
    for (const RunSummary& summary : summaries_) {
        out << "| `" << summary.name << "` | " << summary.expectation << " | "
            << summary.steps << " | " << summary.stateMismatches << " | "
            << summary.frameMismatches << " | " << summary.orderMismatches
            << " | " << summary.repeatFailures << " | "
            << (summary.expectationMet ? "是" : "否") << " |\n";
    }
    out << "\n每个分支的 `runs/<name>/trace.jsonl` 都逐条保存输入、变更前哈希、"
           "结果状态、协议状态、帧 SHA-256 和 PNG 路径。\n\n";

    out << "## 第一个错误状态\n\n";
    if (error == nullptr) {
        out << "没有发现状态或帧差异。\n";
    } else {
        out << "第一个错误状态位于 `" << error->run << "` 的逻辑步骤 "
            << error->logicalStep << "（轨迹步骤 " << error->traceStep
            << "）：收到事件 `" << error->actualEventId << "`，基准期望事件 `"
            << error->expectedEventId << "`。\n\n"
            << "- 实际视觉哈希：`" << error->actualStateSha256 << "`\n"
            << "- 期望视觉哈希：`" << error->expectedStateSha256 << "`\n"
            << "- 实际帧哈希：`" << error->actualFrameSha256 << "`\n"
            << "- 期望帧哈希：`" << error->expectedFrameSha256 << "`\n"
            << "- 像素差异：" << error->differingPixels << " 个像素，最大通道差 "
            << error->maxChannelDelta << "，首个差异坐标 ("
            << error->firstDiffX << ", " << error->firstDiffY << ")\n"
            << "- 实际证据：`" << error->actualPng << "`\n"
            << "- 期望证据：`" << error->expectedPng << "`\n\n"
            << "- 并排图：`first_error_side_by_side.png`（左实际、右期望）\n"
            << "- 差异热图：`first_error_diff.png`（越红表示通道差越大）\n\n"
            << "这里是首个截图状态分歧，而不是汇总平均分；逐步骤比较数据见 "
               "`comparisons.jsonl`。\n";
    }

    out << "\n## 重复提交结论\n\n"
        << "- 完全重复的事件（同一 event id）由事件幂等表直接丢弃，不改变视觉状态。\n"
        << "- 使用新 event id 再次提交同一 token `rev-2` 时，按 submission token "
           "去重；协议轨迹前进但截图保持与基准末态一致。\n"
        << "- 两个分支都执行了精确二次渲染，PNG 由 Vulkan 回读的 RGBA 像素生成。\n\n"
        << "## 关键证据\n\n"
        << "- `report.json`：机器可读汇总和首个错误状态。\n"
        << "- `first_error.json`：首个分歧状态单列证据。\n"
        << "- `first_error_side_by_side.png` / `first_error_diff.png`：首个错误状态的"
           "并排截图与像素差异热图。\n"
        << "- `comparisons.jsonl`：每条逐步状态/帧/顺序比较。\n"
        << "- `runs/*/trace.jsonl` 与 `runs/*/frames/*.png`：完整输入、状态、结果和截图。\n"
        << "- `inputs/truncated.record`：真实未换行截断输入。\n"
        << "- `evidence.sha256`：全部证据文件的内容哈希。\n";
    return out.str();
}

void Experiment::writeReports() {
    {
        const fs::path path = output_ / "comparisons.jsonl";
        std::ofstream output(path, std::ios::binary);
        if (!output) throw std::runtime_error("cannot write " + path.string());
        for (const Comparison& comparison : comparisons_) {
            output << comparisonJson(comparison);
        }
    }

    const Comparison* error = firstError();
    {
        const fs::path path = output_ / "first_error.json";
        std::ofstream output(path, std::ios::binary);
        if (!output) throw std::runtime_error("cannot write " + path.string());
        if (error == nullptr) {
            output << "null\n";
        } else {
            output << comparisonJson(*error);
        }
    }
    if (error != nullptr && error->actualRgba.size() ==
                                 error->expectedRgba.size() &&
        error->actualRgba.size() ==
            static_cast<std::size_t>(options_.width) * options_.height * 4) {
        const std::size_t rowBytes = static_cast<std::size_t>(options_.width) * 4;
        const std::uint32_t sideWidth = options_.width * 2 + 4;
        Bytes side(static_cast<std::size_t>(sideWidth) * options_.height * 4, 0);
        for (std::size_t i = 3; i < side.size(); i += 4) side[i] = 255;
        for (std::uint32_t y = 0; y < options_.height; ++y) {
            const std::size_t source = static_cast<std::size_t>(y) * rowBytes;
            const std::size_t left = static_cast<std::size_t>(y) * sideWidth * 4;
            const std::size_t right = left + (options_.width + 4) * 4;
            std::copy_n(error->actualRgba.begin() +
                            static_cast<std::ptrdiff_t>(source),
                        rowBytes,
                        side.begin() + static_cast<std::ptrdiff_t>(left));
            std::copy_n(error->expectedRgba.begin() +
                            static_cast<std::ptrdiff_t>(source),
                        rowBytes,
                        side.begin() + static_cast<std::ptrdiff_t>(right));
        }
        Bytes diff(rowBytes * options_.height, 0);
        for (std::uint32_t y = 0; y < options_.height; ++y) {
            for (std::uint32_t x = 0; x < options_.width; ++x) {
                const std::size_t pixel =
                    (static_cast<std::size_t>(y) * options_.width + x) * 4;
                std::uint32_t delta = 0;
                for (int channel = 0; channel < 4; ++channel) {
                    const std::uint8_t a = error->actualRgba[pixel + channel];
                    const std::uint8_t b = error->expectedRgba[pixel + channel];
                    delta = std::max(delta,
                                     static_cast<std::uint32_t>(a > b ? a - b : b - a));
                }
                const std::uint8_t light =
                    static_cast<std::uint8_t>(255 - std::min(delta * 2, 255u));
                diff[pixel + 0] = 255;
                diff[pixel + 1] = light;
                diff[pixel + 2] = light;
                diff[pixel + 3] = 255;
            }
        }
        writeRgbaPng(output_ / "first_error_diff.png", options_.width,
                     options_.height, diff);
        writeRgbaPng(output_ / "first_error_side_by_side.png", sideWidth,
                     options_.height, side);
    }

    const std::string report = summaryJson();
    writeText(output_ / "report.json", report);
    writeText(output_ / "report.md", summaryMarkdown());
    writeText(output_ / "environment.json",
              "{\"generated_at\":\"" + utcNow() + "\",\"cxx_standard\":\"C++17\","
              "\"vulkan_device\":\"" + jsonEscape(renderer_.deviceName()) +
                  "\",\"vulkan_driver\":\"" + jsonEscape(renderer_.driverName()) +
                  "\",\"vulkan_api\":\"" + jsonEscape(renderer_.apiVersion()) +
                  "\",\"glfw_platform\":\"" + jsonEscape(renderer_.glfwPlatform()) +
                  "\",\"format\":\"" + jsonEscape(renderer_.vulkanFormat()) +
                  "\",\"width\":" + std::to_string(options_.width) +
                  ",\"height\":" + std::to_string(options_.height) + "}\n");
}

void Experiment::writeManifest() {
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry :
         fs::recursive_directory_iterator(output_)) {
        if (entry.is_regular_file() &&
            entry.path().filename() != "evidence.sha256") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    std::ostringstream manifest;
    for (const fs::path& file : files) {
        const std::string digest = sha256Hex(readBytes(file));
        manifest << digest << "  " << fs::relative(file, output_).generic_string()
                 << '\n';
    }
    writeText(output_ / "evidence.sha256", manifest.str());
}

int runExperiment(const ExperimentOptions& options) {
    Experiment experiment(options);
    return experiment.run();
}

}  // namespace offscreen
