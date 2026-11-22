#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "skincheck/core.hpp"
#include "skincheck/persist.hpp"
#include "skincheck/sha256.hpp"

#if SKINCHECK_HAVE_GUI
#include "skincheck/gui.hpp"
#endif

namespace {

using namespace skincheck;
namespace fs = std::filesystem;

std::string utc_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string make_run_id(const Capture& capture) {
    const std::string digest = sha256_hex(capture_to_json(capture).dump(0));
    return "run-" + digest.substr(0, 16);
}

std::string read_text(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path);
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

Capture load_capture(const std::string& path) {
    std::string error;
    const Json json = Json::parse(read_text(path), &error);
    if (!error.empty()) throw std::runtime_error(path + ": " + error);
    return capture_from_json(json);
}

Checkpoint load_checkpoint(const std::string& path) {
    std::string error;
    const Json json = Json::parse(read_text(path), &error);
    if (!error.empty()) throw std::runtime_error(path + ": " + error);
    return checkpoint_from_json(json);
}

std::int64_t parse_int64(const std::string& text, const std::string& option) {
    try {
        std::size_t used = 0;
        const long long value = std::stoll(text, &used);
        if (used != text.size()) throw std::invalid_argument("trailing characters");
        return static_cast<std::int64_t>(value);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid value for " + option + ": " + text);
    }
}

struct Options {
    std::string command;
    std::string capture;
    std::string checkpoint;
    std::string run;
    std::string out;
    std::int64_t pause_at = -1;
    std::int64_t memory_budget = 16 * 1024 * 1024;
    bool degraded = false;
    bool have_pause_at = false;
    bool help = false;
};

Options parse_options(int argc, char** argv) {
    Options options;
    if (argc > 1) options.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + name);
            return argv[++i];
        };
        if (arg == "--capture") options.capture = value(arg);
        else if (arg == "--checkpoint") options.checkpoint = value(arg);
        else if (arg == "--run") options.run = value(arg);
        else if (arg == "--out") options.out = value(arg);
        else if (arg == "--pause-at") {
            options.pause_at = parse_int64(value(arg), arg);
            options.have_pause_at = true;
        } else if (arg == "--memory-budget-bytes") {
            options.memory_budget = parse_int64(value(arg), arg);
        } else if (arg == "--memory-budget-kib") {
            options.memory_budget = parse_int64(value(arg), arg) * 1024;
        } else if (arg == "--degraded") {
            options.degraded = true;
            options.memory_budget = 4096;
        } else if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }
    return options;
}

void print_usage() {
    std::cout
        << "skincheck - pausable animation skin-weight verification\n\n"
        << "Usage:\n"
        << "  skincheck demo [--out DIR] [--degraded]\n"
        << "  skincheck run --capture FILE --out DIR [--pause-at N]\n"
        << "               [--memory-budget-bytes N | --memory-budget-kib N]\n"
        << "  skincheck resume --checkpoint FILE --out DIR\n"
        << "  skincheck verify --run DIR\n"
        << "  skincheck gui --run DIR\n\n"
        << "Pause defaults to the first detected missing segment.\n";
}

struct PauseOutcome {
    RunSnapshot snapshot;
    std::vector<FrameResult> processed;
};

PauseOutcome pause_run(const Capture& capture, const ResourcePolicy& policy, int pause_at,
                       std::string run_id) {
    const Timeline timeline = build_timeline(capture, policy.min_gap_frames);
    const ResourcePlan resources = plan_resources(capture, timeline, policy);
    const int total = static_cast<int>(timeline.slots.size());
    if (pause_at < 0) {
        pause_at = !timeline.missing_segments.empty()
                       ? timeline.missing_segments.front().first_frame
                       : total;
    }
    pause_at = std::max(0, std::min(pause_at, total));

    PauseOutcome outcome;
    outcome.processed = run_frame_range(timeline, 0, pause_at, resources);
    Checkpoint checkpoint;
    checkpoint.run_id = std::move(run_id);
    checkpoint.phase = "paused";
    checkpoint.capture_sha256 = sha256_hex(capture_to_json(capture).dump(0));
    checkpoint.capture = capture;
    checkpoint.policy = policy;
    checkpoint.resources = resources;
    checkpoint.timeline = timeline;
    checkpoint.next_frame = pause_at;
    checkpoint.completed_frames = outcome.processed;
    checkpoint.summary = summarize(outcome.processed);
    checkpoint.pause_reason = (!timeline.missing_segments.empty() &&
                               pause_at == timeline.missing_segments.front().first_frame)
                                  ? "operator_pause_at_missing_segment_boundary"
                                  : "operator_requested_pause";

    outcome.snapshot.run_id = checkpoint.run_id;
    outcome.snapshot.created_utc = utc_now();
    outcome.snapshot.phase = "paused";
    outcome.snapshot.capture = capture;
    outcome.snapshot.policy = policy;
    outcome.snapshot.resources = resources;
    outcome.snapshot.timeline = timeline;
    outcome.snapshot.checkpoint = checkpoint;
    outcome.snapshot.has_checkpoint = true;
    outcome.snapshot.before_frames = outcome.processed;
    outcome.snapshot.final_frames = outcome.processed;
    outcome.snapshot.comparison.before_status = checkpoint.summary.status;
    outcome.snapshot.comparison.after_status = "not_resumed";
    outcome.snapshot.comparison.stable = false;
    return outcome;
}

RunSnapshot resume_run(const Checkpoint& checkpoint, std::vector<FrameResult>& final_frames) {
    const std::string capture_sha256 =
        sha256_hex(capture_to_json(checkpoint.capture).dump(0));
    if (capture_sha256 != checkpoint.capture_sha256) {
        throw std::runtime_error("checkpoint capture hash does not match embedded capture");
    }
    const Timeline timeline =
        build_timeline(checkpoint.capture, checkpoint.policy.min_gap_frames);
    if (static_cast<int>(timeline.slots.size()) != checkpoint.timeline.slot_count) {
        throw std::runtime_error("checkpoint timeline cannot be rebuilt");
    }
    const ResourcePlan resources =
        plan_resources(checkpoint.capture, timeline, checkpoint.policy);
    if (resources.mode != checkpoint.resources.mode) {
        throw std::runtime_error("checkpoint resource mode changed on reload");
    }

    final_frames = checkpoint.completed_frames;
    const auto continued = run_frame_range(
        timeline, checkpoint.next_frame, static_cast<int>(timeline.slots.size()), resources);
    final_frames.insert(final_frames.end(), continued.begin(), continued.end());
    const ResumeComparison comparison =
        compare_resume_results(timeline, resources, checkpoint.completed_frames, final_frames);

    RunSnapshot snapshot;
    snapshot.run_id = checkpoint.run_id;
    snapshot.created_utc = utc_now();
    snapshot.phase = "complete";
    snapshot.capture = checkpoint.capture;
    snapshot.policy = checkpoint.policy;
    snapshot.resources = resources;
    snapshot.timeline = timeline;
    snapshot.checkpoint = checkpoint;
    snapshot.has_checkpoint = true;
    snapshot.before_frames = checkpoint.completed_frames;
    snapshot.final_frames = final_frames;
    snapshot.comparison = comparison;
    return snapshot;
}

int command_run(const Options& options) {
    if (options.capture.empty() || options.out.empty()) {
        throw std::runtime_error("run requires --capture and --out");
    }
    ResourcePolicy policy;
    policy.memory_budget_bytes = options.memory_budget;
    const Capture capture = load_capture(options.capture);
    const int pause_at = options.have_pause_at ? static_cast<int>(options.pause_at) : -1;
    const PauseOutcome outcome =
        pause_run(capture, policy, pause_at, make_run_id(capture));
    const ExportResult export_result = export_run(options.out, outcome.snapshot);
    if (!export_result.ok) throw std::runtime_error(export_result.error);
    std::cout << "paused at frame " << outcome.snapshot.checkpoint.next_frame << "; wrote "
              << (fs::path(options.out) / "checkpoint.json").string() << "\n";
    std::cout << "resource mode: " << resource_mode_name(outcome.snapshot.resources.mode) << "\n";
    return 0;
}

int command_resume(const Options& options) {
    if (options.checkpoint.empty() || options.out.empty()) {
        throw std::runtime_error("resume requires --checkpoint and --out");
    }
    const Checkpoint checkpoint = load_checkpoint(options.checkpoint);
    std::vector<FrameResult> final_frames;
    const RunSnapshot snapshot = resume_run(checkpoint, final_frames);
    const ExportResult export_result = export_run(options.out, snapshot);
    if (!export_result.ok) throw std::runtime_error(export_result.error);
    std::cout << "resumed at frame " << checkpoint.next_frame << "; rechecked "
              << snapshot.comparison.matched << "/" << snapshot.comparison.frames_rechecked
              << " frames, mismatches " << snapshot.comparison.mismatched << "\n";
    std::cout << "final status: " << summarize(snapshot.final_frames).status << "\n";
    return snapshot.comparison.stable ? 0 : 3;
}

int command_verify(const Options& options) {
    if (options.run.empty()) throw std::runtime_error("verify requires --run");
    const VerifyResult result = verify_run(options.run);
    for (const std::string& message : result.messages) {
        std::cout << message << "\n";
    }
    if (!result.report_path.empty()) {
        std::cout << "report: " << result.report_path << "\n";
    }
    return result.ok ? 0 : 2;
}

int command_demo(const Options& options) {
    const fs::path root = options.out.empty() ? fs::path("artifacts/demo") : fs::path(options.out);
    ResourcePolicy policy;
    policy.memory_budget_bytes = options.degraded ? 4096 : 16 * 1024 * 1024;
    const Capture capture = make_demo_capture();
    const std::string run_id = make_run_id(capture);

    const PauseOutcome paused = pause_run(capture, policy, 8, run_id);
    const ExportResult paused_export = export_run((root / "paused").string(), paused.snapshot);
    if (!paused_export.ok) throw std::runtime_error(paused_export.error);

    const Checkpoint checkpoint = load_checkpoint((root / "paused" / "checkpoint.json").string());
    std::vector<FrameResult> final_frames;
    const RunSnapshot complete = resume_run(checkpoint, final_frames);
    const ExportResult final_export = export_run((root / "final").string(), complete);
    if (!final_export.ok) throw std::runtime_error(final_export.error);

    const VerifyResult paused_verify = verify_run((root / "paused").string());
    const VerifyResult final_verify = verify_run((root / "final").string());
    std::cout << "demo artifacts: " << root.string() << "\n";
    std::cout << "resource mode: " << resource_mode_name(complete.resources.mode) << "\n";
    std::cout << "missing segment: "
              << (complete.timeline.missing_segments.empty()
                      ? "none"
                      : complete.timeline.missing_segments.front().device_id + " frames " +
                            std::to_string(complete.timeline.missing_segments.front().first_frame) +
                            ".." +
                            std::to_string(complete.timeline.missing_segments.front().last_frame))
              << "\n";
    std::cout << "resume comparison: " << complete.comparison.matched << "/"
              << complete.comparison.frames_rechecked << " matched, stable="
              << (complete.comparison.stable ? "true" : "false") << "\n";
    std::cout << "paused verify: " << (paused_verify.ok ? "PASS" : "FAIL")
              << "; final verify: " << (final_verify.ok ? "PASS" : "FAIL") << "\n";
    return paused_verify.ok && final_verify.ok && complete.comparison.stable ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        if (options.help || options.command.empty() || options.command == "help") {
            print_usage();
            return options.command.empty() && !options.help ? 1 : 0;
        }
        if (options.command == "demo") return command_demo(options);
        if (options.command == "run") return command_run(options);
        if (options.command == "resume") return command_resume(options);
        if (options.command == "verify") return command_verify(options);
        if (options.command == "gui") {
#if SKINCHECK_HAVE_GUI
            if (options.run.empty()) throw std::runtime_error("gui requires --run");
            return run_gui(options.run);
#else
            throw std::runtime_error("this build has no Vulkan/GLFW viewer");
#endif
        }
        throw std::runtime_error("unknown command: " + options.command);
    } catch (const std::exception& exception) {
        std::cerr << "error: " << exception.what() << "\n";
        return 1;
    }
}
