#include "cli_commands.hpp"

#include "camtrak/model.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace camtrak {
namespace {

std::string optionOf(
    const std::vector<std::pair<std::string, std::string>>& options,
    const std::string& name, const std::string& fallback = "") {
  for (const auto& [key, value] : options) {
    if (key == name) return value;
  }
  return fallback;
}

double requireNumber(
    const std::vector<std::pair<std::string, std::string>>& options,
    const std::string& name) {
  const std::string raw = optionOf(options, name);
  if (raw.empty()) throw std::runtime_error("missing --" + name);
  return std::stod(raw);
}

Quat quatFromAngleAxis(double angle, const Vec3& axis) {
  const double half = angle * 0.5;
  const double sine = std::sin(half);
  return normalize(Quat{std::cos(half), axis.x * sine, axis.y * sine,
                        axis.z * sine});
}

// Two render objects captured on two devices. Device B's clock runs ahead of
// device A, and a segment of each trajectory is dropped to emulate the gap
// caused by the clock desync / dropped tracking.
Project buildDemoProject() {
  Project project;
  project.project_id = "demo_clock_desync";
  project.created_at = utcNowIso8601();
  project.description =
      "demo project: two-device clock desync with a missing trajectory gap";
  project.clocks.offset_seconds["devA"] = 0.0;
  project.clocks.scale["devA"] = 1.0;
  project.clocks.offset_seconds["devB"] = 0.35;
  project.clocks.scale["devB"] = 1.0;
  project.object_ids = {"object_alpha", "object_beta"};
  project.input_files["object_alpha"] = "inputs/object_alpha.json";
  project.input_files["object_beta"] = "inputs/object_beta.json";
  return project;
}

Trajectory buildDemoTrajectory(const std::string& object_id,
                               double phase) {
  Trajectory trajectory;
  trajectory.object_id = object_id;
  for (int i = 0; i <= 40; ++i) {
    const double reference_time = i * 0.1;
    // Drop the desync window: no samples on the reference interval [2.0,3.0).
    if (reference_time >= 2.0 && reference_time < 3.0) continue;
    Sample sample;
    sample.device_id = (i % 2 == 0) ? "devA" : "devB";
    // Encode device-local timestamps by inverting the clock mapping.
    const double offset = sample.device_id == "devB" ? 0.35 : 0.0;
    sample.t_device = reference_time - offset;
    sample.pose.position =
        Vec3{std::cos(reference_time + phase),
             std::sin(reference_time + phase), 0.5 * reference_time};
    sample.pose.orientation =
        quatFromAngleAxis(0.1 * reference_time + phase, Vec3{0, 0, 1});
    trajectory.samples.push_back(sample);
  }
  sortAndValidate(trajectory);
  return trajectory;
}

}  // namespace

int cliInitDemo(const std::string& project_dir) {
  Project project = buildDemoProject();
  Store::initialize(project_dir, project);
  Store store(project_dir);
  for (const std::string& object_id : project.object_ids) {
    const Trajectory trajectory =
        buildDemoTrajectory(object_id, object_id == "object_alpha" ? 0.0 : 1.2);
    store.saveTrajectory(object_id, trajectory);
  }
  store.rebuildEvidenceManifest();
  std::cout << "initialized demo project at " << project_dir << "\n"
            << "objects: object_alpha, object_beta\n"
            << "clock offset: devB = +0.35s vs devA\n"
            << "missing gap on reference timeline: [2.0, 3.0)\n";
  return 0;
}

int cliStatus(Store& store) {
  const Project project = store.loadProject();
  const auto session = store.loadSession();
  const auto runs = store.listRunIds();
  std::cout << "project: " << project.project_id << "\n";
  std::cout << "created: " << project.created_at << "\n";
  std::cout << "clock mappings:\n";
  for (const auto& [device, offset] : project.clocks.offset_seconds) {
    const double scale = project.clocks.scale.count(device)
                             ? project.clocks.scale.at(device)
                             : 1.0;
    std::cout << "  " << device << ": offset=" << offset
              << "s scale=" << scale << "\n";
  }
  std::cout << "objects (" << project.object_ids.size() << "):\n";
  for (const std::string& object_id : project.object_ids) {
    std::cout << "  - " << object_id;
    auto found = session.find(object_id);
    if (found != session.end()) {
      std::cout << " [session: " << found->second.status
                << " run=" << found->second.run_id << "]";
    }
    std::cout << "\n";
  }
  std::cout << "stored runs: " << runs.size() << "\n";
  for (const std::string& run_id : runs) {
    std::cout << "  - " << run_id << "\n";
  }
  return 0;
}

namespace {

CorrectionConfig configFromOptions(
    const std::vector<std::pair<std::string, std::string>>& options) {
  CorrectionConfig config;
  config.object_id = optionOf(options, "object");
  if (config.object_id.empty()) {
    throw std::runtime_error("missing --object");
  }
  config.gap_t0 = requireNumber(options, "t0");
  config.gap_t1 = requireNumber(options, "t1");
  config.sample_period =
      std::stod(optionOf(options, "period", "0.1"));
  config.method = parseMethod(optionOf(options, "method", "linear"));
  return config;
}

void printReport(const EngineReport& report) {
  const RunResult& result = report.result;
  std::cout << (report.resumed ? "resumed run: " : "created run: ")
            << result.run_id << "\n"
            << "object: " << result.object_id
            << " method: " << result.method << "\n"
            << "gap: [" << result.config.gap_t0 << ", "
            << result.config.gap_t1 << "] period="
            << result.config.sample_period << "\n"
            << "corrected samples: " << result.corrected.size() << "\n"
            << "metrics: compared=" << result.metrics.compared_samples
            << " max_pos_err=" << result.metrics.max_position_error
            << " rmse_pos=" << result.metrics.rmse_position
            << " max_angle_err=" << result.metrics.max_angle_error_rad
            << "\n"
            << "log entry hash: " << report.log_entry.entry_hash << "\n";
}

}  // namespace

int cliRun(Store& store, Engine& engine,
           const std::vector<std::pair<std::string, std::string>>& options) {
  (void)store;
  const CorrectionConfig config = configFromOptions(options);
  const EngineReport report = engine.runObject(config);
  printReport(report);
  return 0;
}

int cliResume(Store& store, Engine& engine,
              const std::vector<std::pair<std::string, std::string>>&
                  options) {
  const std::string object_id = optionOf(options, "object");
  if (object_id.empty()) throw std::runtime_error("missing --object");
  const auto session = store.loadSession();
  auto found = session.find(object_id);
  if (found == session.end() || found->second.status != "running") {
    throw std::runtime_error(
        "no resumable running session for object " + object_id);
  }
  const EngineReport report = engine.runObject(found->second.config);
  printReport(report);
  return 0;
}

int cliCompare(Store& store, Engine& engine,
               const std::vector<std::pair<std::string, std::string>>&
                   options) {
  (void)store;
  const std::string run_a = optionOf(options, "run-a");
  const std::string run_b = optionOf(options, "run-b");
  if (run_a.empty() || run_b.empty()) {
    throw std::runtime_error("compare needs --run-a and --run-b");
  }
  const Engine::Comparison comparison = engine.compare(run_a, run_b);
  std::cout << "comparison " << run_a << " vs " << run_b << "\n"
            << "common samples: " << comparison.common_samples << "\n"
            << "max position delta: "
            << comparison.max_position_delta << "\n"
            << "rmse position delta: "
            << comparison.rmse_position_delta << "\n"
            << "max angle delta (rad): "
            << comparison.max_angle_delta_rad << "\n";
  return 0;
}

int cliExport(Store& store,
              const std::vector<std::pair<std::string, std::string>>&
                  options) {
  const std::string batch_id = optionOf(options, "batch");
  if (batch_id.empty()) throw std::runtime_error("missing --batch");
  const std::string runs_raw = optionOf(options, "runs");
  if (runs_raw.empty()) throw std::runtime_error("missing --runs");
  std::vector<std::string> run_ids;
  std::stringstream stream(runs_raw);
  std::string token;
  while (std::getline(stream, token, ',')) {
    if (!token.empty()) run_ids.push_back(token);
  }
  const std::string path = store.createExport(
      batch_id, run_ids, optionOf(options, "note", ""));
  std::cout << "exported review bundle to " << path << "\n";
  return 0;
}

int cliVerify(Store& store, Engine& engine,
              const std::vector<std::pair<std::string, std::string>>&
                  options) {
  int failures_total = 0;
  std::vector<std::string> evidence_failures;
  if (!store.verifyEvidence(&evidence_failures)) {
    for (const std::string& failure : evidence_failures) {
      std::cout << "EVIDENCE FAILURE: " << failure << "\n";
      ++failures_total;
    }
  } else {
    std::cout << "evidence snapshots: OK\n";
  }

  std::vector<std::string> log_failures;
  if (!store.verifyRunLog(&log_failures)) {
    for (const std::string& failure : log_failures) {
      std::cout << "LOG FAILURE: " << failure << "\n";
      ++failures_total;
    }
  } else {
    std::cout << "run log hash chain: OK\n";
  }

  std::vector<std::string> run_ids;
  const std::string single = optionOf(options, "run");
  if (!single.empty()) {
    run_ids.push_back(single);
  } else {
    run_ids = store.listRunIds();
  }
  for (const std::string& run_id : run_ids) {
    const std::vector<std::string> failures = engine.verifyRun(run_id);
    if (failures.empty()) {
      std::cout << "run " << run_id << ": VERIFIED\n";
    } else {
      for (const std::string& failure : failures) {
        std::cout << "RUN FAILURE [" << run_id << "]: " << failure << "\n";
        ++failures_total;
      }
    }
  }
  return failures_total == 0 ? 0 : 1;
}

int cliLog(Store& store) {
  const auto entries = store.readRunLog();
  for (const RunLogEntry& entry : entries) {
    std::cout << entry.recorded_at << " " << entry.run_id << " "
              << entry.object_id << " " << entry.status
              << " config=" << entry.config_hash
              << " result=" << entry.result_hash
              << " entry=" << entry.entry_hash << "\n";
  }
  if (entries.empty()) std::cout << "(no runs recorded yet)\n";
  return 0;
}

}  // namespace camtrak
