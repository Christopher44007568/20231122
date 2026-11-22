#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "skincheck/core.hpp"
#include "skincheck/json.hpp"
#include "skincheck/persist.hpp"
#include "skincheck/sha256.hpp"

namespace {

using namespace skincheck;
namespace fs = std::filesystem;

int failures = 0;
int checks = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL " << file << ":" << line << "  " << expression << "\n";
    }
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

void write_text(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write " + path.string());
    output << text;
}

std::string read_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

fs::path temporary_directory(const std::string& name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("skincheck-test-" + name + "-" + std::to_string(stamp));
}

void test_sha256() {
    CHECK(sha256_hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

void test_ordered_json_roundtrip() {
    const std::string text = "{\"z\":1,\"a\":{\"y\":2,\"b\":3},\"m\":[true,null,\"x\"]}";
    std::string error;
    const Json parsed = Json::parse(text, &error);
    CHECK(error.empty());
    CHECK(parsed.dump(0) == text);
    const std::vector<std::string> keys = parsed.keys();
    CHECK((keys == std::vector<std::string>{"z", "a", "m"}));
    const Json* nested = parsed.find("a");
    CHECK(nested != nullptr && (nested->keys() == std::vector<std::string>{"y", "b"}));
    const Json reparsed = Json::parse(parsed.dump(2), &error);
    CHECK(error.empty());
    CHECK(reparsed.dump(0) == text);
}

void test_timeline_and_checks() {
    const Capture capture = make_demo_capture();
    const Timeline timeline = build_timeline(capture, 3);
    CHECK(timeline.slots.size() == 24);
    CHECK(timeline.slot_count == 24);
    CHECK(timeline.missing_segments.size() == 1);
    if (timeline.missing_segments.size() == 1) {
        const MissingSegment& gap = timeline.missing_segments.front();
        CHECK(gap.device_id == "device-b");
        CHECK(gap.first_frame == 8);
        CHECK(gap.last_frame == 11);
        CHECK(gap.reference_start_us == 80000);
        CHECK(gap.reference_end_us == 120000);
        CHECK(gap.nominal_local_gap_us == 50000);
        CHECK(gap.clock_skew_effect_us == 200);
        CHECK(gap.reason == "unsynchronized_clock_projection_gap");
    }

    ResourcePolicy policy;
    policy.memory_budget_bytes = 16 * 1024 * 1024;
    const ResourcePlan plan = plan_resources(capture, timeline, policy);
    CHECK(plan.mode == ResourceMode::Full);
    const std::vector<FrameResult> before = run_frame_range(timeline, 0, 8, plan);
    const std::vector<FrameResult> after = run_frame_range(timeline, 8, 24, plan);
    CHECK(before.size() == 8);
    CHECK(after.size() == 16);
    CHECK(summarize(before).fail == 2);
    CHECK(summarize(before).issues == 3);
    CHECK(after[0].status == "incomplete");
    CHECK(after[1].status == "fail_incomplete");
    CHECK(after[4].status == "pass");

    std::vector<FrameResult> all = before;
    all.insert(all.end(), after.begin(), after.end());
    const Summary summary = summarize(all);
    CHECK(summary.frames == 24);
    CHECK(summary.pass == 17);
    CHECK(summary.fail == 4);
    CHECK(summary.incomplete == 3);
    CHECK(summary.issues == 5);
    CHECK(summary.status == "fail");
}

void test_degraded_path() {
    const Capture capture = make_demo_capture();
    const Timeline timeline = build_timeline(capture, 3);
    ResourcePolicy policy;
    policy.memory_budget_bytes = 4096;
    const ResourcePlan plan = plan_resources(capture, timeline, policy);
    CHECK(plan.mode == ResourceMode::Degraded);
    CHECK((plan.skipped_checks == std::vector<std::string>{"duplicate_bone"}));
    const std::vector<FrameResult> frames = run_frame_range(timeline, 0, 24, plan);
    const Summary summary = summarize(frames);
    CHECK(summary.issues == 4);
    CHECK(summary.degraded == 18);
    CHECK(frames[15].status == "pass_degraded");

    policy.allow_degraded = false;
    bool rejected = false;
    try {
        plan_resources(capture, timeline, policy);
    } catch (const std::exception&) {
        rejected = true;
    }
    CHECK(rejected);
}

struct Pipeline {
    RunSnapshot paused;
    RunSnapshot complete;
    Checkpoint checkpoint;
};

Pipeline make_pipeline(const Capture& capture, const ResourcePolicy& policy, int pause_at) {
    const Timeline timeline = build_timeline(capture, policy.min_gap_frames);
    const ResourcePlan resources = plan_resources(capture, timeline, policy);
    Pipeline pipeline;
    pipeline.paused.run_id = "test-run";
    pipeline.paused.created_utc = "2026-11-22T00:00:00Z";
    pipeline.paused.phase = "paused";
    pipeline.paused.capture = capture;
    pipeline.paused.policy = policy;
    pipeline.paused.resources = resources;
    pipeline.paused.timeline = timeline;
    pipeline.paused.before_frames = run_frame_range(timeline, 0, pause_at, resources);
    pipeline.paused.final_frames = pipeline.paused.before_frames;
    pipeline.paused.comparison.before_status =
        summarize(pipeline.paused.before_frames).status;
    pipeline.paused.comparison.after_status = "not_resumed";
    pipeline.paused.comparison.stable = false;

    pipeline.checkpoint.schema = "skincheck.checkpoint/v1";
    pipeline.checkpoint.run_id = pipeline.paused.run_id;
    pipeline.checkpoint.phase = "paused";
    pipeline.checkpoint.pause_reason = "operator_pause_at_missing_segment_boundary";
    pipeline.checkpoint.capture_sha256 = sha256_hex(capture_to_json(capture).dump(0));
    pipeline.checkpoint.capture = capture;
    pipeline.checkpoint.policy = policy;
    pipeline.checkpoint.resources = resources;
    pipeline.checkpoint.timeline = timeline;
    pipeline.checkpoint.next_frame = pause_at;
    pipeline.checkpoint.completed_frames = pipeline.paused.before_frames;
    pipeline.checkpoint.summary = summarize(pipeline.paused.before_frames);
    pipeline.paused.checkpoint = pipeline.checkpoint;
    pipeline.paused.has_checkpoint = true;

    pipeline.complete.run_id = pipeline.paused.run_id;
    pipeline.complete.created_utc = "2026-11-22T00:00:01Z";
    pipeline.complete.phase = "complete";
    pipeline.complete.capture = capture;
    pipeline.complete.policy = policy;
    pipeline.complete.resources = resources;
    pipeline.complete.timeline = timeline;
    pipeline.complete.checkpoint = pipeline.checkpoint;
    pipeline.complete.has_checkpoint = true;
    pipeline.complete.before_frames = pipeline.checkpoint.completed_frames;
    pipeline.complete.final_frames = pipeline.checkpoint.completed_frames;
    const auto continued =
        run_frame_range(timeline, pause_at, static_cast<int>(timeline.slots.size()), resources);
    pipeline.complete.final_frames.insert(pipeline.complete.final_frames.end(),
                                          continued.begin(), continued.end());
    pipeline.complete.comparison =
        compare_resume_results(timeline, resources, pipeline.checkpoint.completed_frames,
                               pipeline.complete.final_frames);
    pipeline.complete.comparison.after_status =
        summarize(pipeline.complete.final_frames).status;
    return pipeline;
}

void test_export_reload_and_verify() {
    const fs::path root = temporary_directory("export");
    const Capture capture = make_demo_capture();
    ResourcePolicy policy;
    const Pipeline pipeline = make_pipeline(capture, policy, 8);
    const ExportResult paused = export_run((root / "paused").string(), pipeline.paused);
    CHECK(paused.ok);
    const ExportResult complete = export_run((root / "final").string(), pipeline.complete);
    CHECK(complete.ok);
    if (!paused.ok || !complete.ok) return;

    const std::string checkpoint_text = read_text(root / "final" / "checkpoint.json");
    std::string error;
    const Json checkpoint_json = Json::parse(checkpoint_text, &error);
    CHECK(error.empty());
    CHECK(checkpoint_json.dump(2) + "\n" == checkpoint_text);
    CHECK(checkpoint_json.keys() == checkpoint_field_order());
    const Checkpoint checkpoint = checkpoint_from_json(checkpoint_json);
    CHECK(checkpoint.completed_frames.size() == 8);
    CHECK(checkpoint.next_frame == 8);

    const std::string run_text = read_text(root / "final" / "run.json");
    const Json run = Json::parse(run_text, &error);
    CHECK(error.empty());
    CHECK(run.dump(2) + "\n" == run_text);
    CHECK(run.keys() == run_field_order());
    const Json& frames = *run.find("frames");
    CHECK(frames.items().size() == 24);
    CHECK(frames.items().front().keys() == frame_field_order());
    CHECK(frames.items()[3].find("issues")->items().front().keys() ==
          issue_field_order());

    const VerifyResult verify = verify_run((root / "final").string());
    CHECK(verify.ok);
    CHECK(verify.failures == 0);
    CHECK(fs::exists(root / "final" / "verification.json"));

    std::ofstream tamper(root / "final" / "frames.jsonl", std::ios::app);
    tamper << " \n";
    tamper.close();
    const VerifyResult tampered = verify_run((root / "final").string());
    CHECK(!tampered.ok);
    CHECK(tampered.failures > 0);
    fs::remove_all(root);
}

void test_json_api_order_guards() {
    Json object = Json::object();
    object.set("first", Json(1));
    object.set("second", Json(2));
    object.set("first", Json(3));
    CHECK((object.keys() == std::vector<std::string>{"first", "second"}));
    CHECK(object.dump(0) == "{\"first\":3,\"second\":2}");
}

}  // namespace

int main() {
    try {
        test_sha256();
        test_ordered_json_roundtrip();
        test_timeline_and_checks();
        test_degraded_path();
        test_export_reload_and_verify();
        test_json_api_order_guards();
    } catch (const std::exception& exception) {
        std::cerr << "UNCAUGHT: " << exception.what() << "\n";
        return 1;
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
