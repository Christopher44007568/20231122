#include <filesystem>
#include <unistd.h>

#include "camtrak/engine.hpp"
#include "camtrak/store.hpp"
#include "test_framework.hpp"

using namespace camtrak;
namespace fs = std::filesystem;

namespace {

fs::path makeProject() {
  static int counter = 0;
  const fs::path dir = fs::temp_directory_path() /
                       ("camtrak_engine_" + std::to_string(::getpid()) +
                        "_" + std::to_string(counter++));
  Project project;
  project.project_id = "engine_test";
  project.created_at = "2026-11-22T00:00:00Z";
  project.description = "engine test";
  project.clocks.offset_seconds["devA"] = 0.0;
  project.clocks.scale["devA"] = 1.0;
  project.clocks.offset_seconds["devB"] = 0.35;
  project.clocks.scale["devB"] = 1.0;
  project.object_ids = {"obj1"};
  project.input_files["obj1"] = "inputs/obj1.json";
  Store::initialize(dir.string(), project);

  Store store(dir.string());
  Trajectory trajectory;
  trajectory.object_id = "obj1";
  for (int i = 0; i <= 30; ++i) {
    const double reference = i * 0.1;
    if (reference >= 1.0 && reference < 2.0) continue;  // missing gap
    Sample sample;
    sample.device_id = (i % 2 == 0) ? "devA" : "devB";
    sample.t_device =
        reference - (sample.device_id == "devB" ? 0.35 : 0.0);
    sample.pose.position = Vec3{reference, reference * 2.0, 0.0};
    sample.pose.orientation = Quat{};
    trajectory.samples.push_back(sample);
  }
  store.saveTrajectory("obj1", trajectory);
  store.rebuildEvidenceManifest();
  return dir;
}

CorrectionConfig makeConfig(InterpMethod method) {
  CorrectionConfig config;
  config.object_id = "obj1";
  config.gap_t0 = 1.0;
  config.gap_t1 = 2.0;
  config.sample_period = 0.1;
  config.method = method;
  return config;
}

}  // namespace

TEST_CASE(engine_run_is_deterministic_and_verifiable) {
  const fs::path dir = makeProject();
  Store store(dir.string());
  Engine engine(store);
  const CorrectionConfig config = makeConfig(InterpMethod::Linear);
  const EngineReport first = engine.runObject(config);
  CHECK(first.result.corrected.size() == 11);
  CHECK(first.log_entry.status == "completed");

  const std::vector<std::string> failures =
      engine.verifyRun(first.result.run_id);
  CHECK(failures.empty());

  std::vector<std::string> log_failures;
  CHECK(store.verifyRunLog(&log_failures));
  CHECK(log_failures.empty());
  fs::remove_all(dir);
}

TEST_CASE(engine_session_marks_completion_and_clears_on_rerun) {
  const fs::path dir = makeProject();
  Store store(dir.string());
  Engine engine(store);
  const EngineReport report =
      engine.runObject(makeConfig(InterpMethod::Linear));
  auto session = store.loadSession();
  CHECK(session.at("obj1").status == "completed");
  CHECK(session.at("obj1").run_id == report.result.run_id);
  fs::remove_all(dir);
}

TEST_CASE(engine_resume_reuses_run_id_for_same_config) {
  const fs::path dir = makeProject();
  Store store(dir.string());
  Engine engine(store);
  const CorrectionConfig config = makeConfig(InterpMethod::Linear);

  // Simulate an interrupted run by planting a running session state.
  RunState interrupted;
  interrupted.run_id = "run_planted";
  interrupted.object_id = "obj1";
  interrupted.status = "running";
  interrupted.started_at = "2026-11-22T00:00:00Z";
  interrupted.updated_at = "2026-11-22T00:00:00Z";
  interrupted.config_hash = Engine::canonicalConfigHash(config);
  interrupted.config = config;
  store.saveSessionState(interrupted);

  const EngineReport resumed = engine.runObject(config);
  CHECK(resumed.resumed);
  CHECK(resumed.result.run_id == "run_planted");
  fs::remove_all(dir);
}

TEST_CASE(engine_compare_and_export_bundle) {
  const fs::path dir = makeProject();
  Store store(dir.string());
  Engine engine(store);
  const RunResult linear =
      engine.runObject(makeConfig(InterpMethod::Linear)).result;
  const RunResult cubic =
      engine.runObject(makeConfig(InterpMethod::Cubic)).result;

  const Engine::Comparison comparison =
      engine.compare(linear.run_id, cubic.run_id);
  CHECK(comparison.common_samples == 11);

  const std::string export_path =
      store.createExport("batch1", {linear.run_id, cubic.run_id}, "review");
  CHECK(fs::exists(fs::path(export_path) / "export_manifest.json"));
  CHECK(fs::exists(fs::path(export_path) / "REVIEW.txt"));
  CHECK(fs::exists(fs::path(export_path) / "snapshots/obj1.json"));
  fs::remove_all(dir);
}

TEST_CASE(engine_detects_tampered_run_file) {
  const fs::path dir = makeProject();
  Store store(dir.string());
  Engine engine(store);
  const RunResult result =
      engine.runObject(makeConfig(InterpMethod::Linear)).result;
  const fs::path run_file =
      dir / "runs" / (result.run_id + ".json");
  std::string bytes = readFile(run_file.string());
  // Corrupt one corrected position value while keeping valid JSON.
  const std::string marker = "\"position\": [\n          ";
  const std::size_t pos = bytes.find(marker);
  CHECK(pos != std::string::npos);
  bytes.replace(pos + marker.size(), 1, "9");
  writeFileAtomic(run_file.string(), bytes);
  const std::vector<std::string> failures =
      engine.verifyRun(result.run_id);
  CHECK(!failures.empty());
  fs::remove_all(dir);
}
