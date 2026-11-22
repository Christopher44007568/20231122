#include <filesystem>
#include <unistd.h>

#include "camtrak/model.hpp"
#include "camtrak/sha256.hpp"
#include "camtrak/store.hpp"
#include "test_framework.hpp"

using namespace camtrak;
namespace fs = std::filesystem;

namespace {

fs::path tempProjectDir() {
  static int counter = 0;
  const fs::path path =
      fs::temp_directory_path() /
      ("camtrak_test_" + std::to_string(::getpid()) + "_" +
       std::to_string(counter++));
  fs::remove_all(path);
  return path;
}

Project sampleProject() {
  Project project;
  project.project_id = "test_project";
  project.created_at = "2026-11-22T00:00:00Z";
  project.description = "unit test project";
  project.clocks.offset_seconds["devA"] = 0.0;
  project.clocks.scale["devA"] = 1.0;
  project.clocks.offset_seconds["devB"] = 0.35;
  project.clocks.scale["devB"] = 1.0;
  project.object_ids = {"obj1"};
  project.input_files["obj1"] = "inputs/obj1.json";
  return project;
}

Trajectory sampleTrajectory() {
  Trajectory trajectory;
  trajectory.object_id = "obj1";
  for (int i = 0; i <= 6; ++i) {
    Sample sample;
    sample.device_id = (i % 2 == 0) ? "devA" : "devB";
    sample.t_device = static_cast<double>(i) -
                      (sample.device_id == "devB" ? 0.35 : 0.0);
    sample.pose.position = Vec3{static_cast<double>(i), 0.0, 0.0};
    sample.pose.orientation = Quat{};
    trajectory.samples.push_back(sample);
  }
  sortAndValidate(trajectory);
  return trajectory;
}

}  // namespace

TEST_CASE(store_project_roundtrip_keeps_fields) {
  const fs::path dir = tempProjectDir();
  Store::initialize(dir.string(), sampleProject());
  Store store(dir.string());
  const Project reloaded = store.loadProject();
  CHECK(reloaded.project_id == "test_project");
  CHECK(reloaded.object_ids == std::vector<std::string>{"obj1"});
  CHECK(camtrak_test::near(
      reloaded.clocks.offset_seconds.at("devB"), 0.35));

  const std::string original = readFile((dir / "project.json").string());
  const std::string re_saved = toJson(reloaded).dump();
  CHECK(original == re_saved);
  fs::remove_all(dir);
}

TEST_CASE(store_trajectory_and_snapshot_evidence) {
  const fs::path dir = tempProjectDir();
  Store::initialize(dir.string(), sampleProject());
  Store store(dir.string());
  const std::string digest = store.saveTrajectory("obj1", sampleTrajectory());
  store.rebuildEvidenceManifest();

  std::vector<std::string> failures;
  CHECK(store.verifyEvidence(&failures));
  CHECK(failures.empty());

  const fs::path snapshot =
      dir / "evidence/snapshots" / digest / "obj1.json";
  CHECK(fs::exists(snapshot));

  // Tamper with the input and confirm verification catches it.
  const fs::path input = dir / "inputs/obj1.json";
  std::string bytes = readFile(input.string());
  bytes += " ";
  writeFileAtomic(input.string(), bytes);
  CHECK(!store.verifyEvidence(&failures));
  CHECK(!failures.empty());
  fs::remove_all(dir);
}

TEST_CASE(store_reference_timeline_applies_clock_mapping) {
  const fs::path dir = tempProjectDir();
  Store::initialize(dir.string(), sampleProject());
  Store store(dir.string());
  store.saveTrajectory("obj1", sampleTrajectory());
  const Trajectory raw = store.loadTrajectory("obj1");
  const Trajectory mapped = store.toReferenceTimeline(raw);
  CHECK_NEAR(mapped.samples[0].t_device, 0.0);
  CHECK_NEAR(mapped.samples[1].t_device, 1.0);
  CHECK_NEAR(mapped.samples.back().t_device, 6.0);
  fs::remove_all(dir);
}
