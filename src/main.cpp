#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "evidence.hpp"
#include "model.hpp"
#include "renderer.hpp"
#include "replay.hpp"
#include "sha256.hpp"

namespace bbox {
namespace {

namespace fs = std::filesystem;

struct Options {
  fs::path data_dir = "data";
  fs::path output_dir = "artifacts/latest";
  std::string render = "none";
  int frames = 2;
  bool visible = false;
};

struct EventRecord {
  int line = 0;
  std::string raw;
  bool parsed = false;
  Update update;
  std::string error;
};

struct ExecutedRun {
  std::string name;
  std::string log_path;
  std::map<std::string, Model> final_models;
  std::string fingerprint;
  std::size_t accepted = 0;
  std::size_t duplicate = 0;
  std::size_t stale = 0;
  std::size_t exception = 0;
  std::size_t rejected = 0;
  std::size_t processed_after_exception = 0;
  int first_exception_step = -1;
  std::unordered_set<std::string> seen_event_ids;
};

struct TempWorkspace {
  fs::path path;
  bool cleaned = false;

  explicit TempWorkspace(const fs::path& source) {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path = fs::temp_directory_path() /
           ("bbox-replay-" + std::to_string(stamp));
    fs::create_directories(path);
    for (const char* name :
         {"snapshot_v1.json", "snapshot_v2.json", "events.ndjson"}) {
      fs::copy_file(source / name, path / name);
    }
  }

  ~TempWorkspace() {
    if (!cleaned) {
      std::error_code ignored;
      fs::remove_all(path, ignored);
    }
  }

  bool cleanup() {
    std::error_code error;
    const bool removed = fs::remove_all(path, error) > 0;
    cleaned = !error && !fs::exists(path);
    return cleaned && (removed || !fs::exists(path));
  }
};

void printUsage(const char* program) {
  std::cout
      << "Usage: " << program << " [options]\n"
      << "  --data-dir PATH     Input snapshots and events (default: data)\n"
      << "  --output-dir PATH   Evidence directory (default: artifacts/latest)\n"
      << "  --render MODE       none | auto | on (default: none)\n"
      << "  --frames N          Vulkan frames to render (default: 2)\n"
      << "  --visible           Show the GLFW window\n"
      << "  --help              Show this help\n";
}

Options parseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    auto next = [&](const char* name) -> std::string {
      if (index + 1 >= argc) {
        throw std::runtime_error(std::string("missing value for ") + name);
      }
      return argv[++index];
    };
    if (argument == "--data-dir") {
      options.data_dir = next("--data-dir");
    } else if (argument == "--output-dir") {
      options.output_dir = next("--output-dir");
    } else if (argument == "--render") {
      options.render = next("--render");
    } else if (argument == "--frames") {
      options.frames = std::stoi(next("--frames"));
    } else if (argument == "--visible") {
      options.visible = true;
    } else if (argument == "--help") {
      printUsage(argv[0]);
      std::exit(0);
    } else {
      throw std::runtime_error("unknown option: " + argument);
    }
  }
  if (options.render != "none" && options.render != "auto" &&
      options.render != "on") {
    throw std::runtime_error("--render must be none, auto, or on");
  }
  if (options.frames < 1) {
    throw std::runtime_error("--frames must be at least 1");
  }
  return options;
}

std::string trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::vector<EventRecord> readEvents(const fs::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  std::vector<EventRecord> records;
  std::string line;
  int line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    const std::string raw = trim(line);
    if (raw.empty()) {
      continue;
    }
    EventRecord record;
    record.line = line_number;
    record.raw = raw;
    record.parsed = parseUpdateJson(raw, record.update, record.error);
    records.push_back(record);
  }
  return records;
}

std::size_t findEvent(const std::vector<EventRecord>& records,
                      const std::string& event_id, int occurrence) {
  int seen = 0;
  for (std::size_t index = 0; index < records.size(); ++index) {
    if (records[index].parsed && records[index].update.event_id == event_id) {
      if (seen == occurrence) {
        return index;
      }
      ++seen;
    }
  }
  throw std::runtime_error("event not found in scenario: " + event_id);
}

picojson::value hashedInput(const fs::path& path) {
  picojson::object object;
  object["path"] = picojson::value(path.string());
  object["sha256"] = picojson::value(sha256File(path.string()));
  object["bytes"] = picojson::value(
      static_cast<double>(fs::file_size(path)));
  return picojson::value(object);
}

ExecutedRun executeRun(const std::string& name, const Snapshot& start,
                       const std::vector<EventRecord>& records,
                       const std::vector<std::size_t>& order,
                       const fs::path& output_dir,
                       const std::unordered_set<std::string>* prior_seen = nullptr);
ExecutedRun executeTruncatedControl(const EventRecord& record,
                                    const Snapshot& start,
                                    const fs::path& output_dir);
picojson::value runJson(const ExecutedRun& run);
picojson::value checkJson(const std::string& name, bool passed,
                          const std::string& evidence);
Snapshot snapshotFromModels(const std::map<std::string, Model>& models);
bool modelEqual(const Model& left, const Model& right);
bool modelsMatchSnapshot(const std::map<std::string, Model>& models,
                         const Snapshot& expected);
void writeManifest(const fs::path& output_dir);

}  // namespace
}  // namespace bbox

int main(int argc, char** argv) {
  using namespace bbox;
  try {
    const Options options = parseOptions(argc, argv);
    if (options.output_dir.filename().empty()) {
      throw std::runtime_error("--output-dir must name a directory");
    }
    std::error_code remove_error;
    fs::remove_all(options.output_dir, remove_error);
    if (remove_error) {
      throw std::runtime_error("cannot reset output directory: " +
                               remove_error.message());
    }
    ensureDirectory(options.output_dir.string());

    const fs::path original_v1 = options.data_dir / "snapshot_v1.json";
    const fs::path original_v2 = options.data_dir / "snapshot_v2.json";
    const fs::path original_events = options.data_dir / "events.ndjson";
    const picojson::value input_v1 = hashedInput(original_v1);
    const picojson::value input_v2 = hashedInput(original_v2);
    const picojson::value input_events = hashedInput(original_events);

    TempWorkspace workspace(options.data_dir);
    fs::copy_file(original_v1, options.output_dir / "snapshot_v1.json",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(original_v2, options.output_dir / "snapshot_v2.json",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(original_events, options.output_dir / "events.ndjson",
                  fs::copy_options::overwrite_existing);

    const Snapshot v1 = loadSnapshot((workspace.path / "snapshot_v1.json").string());
    const Snapshot v2 = loadSnapshot((workspace.path / "snapshot_v2.json").string());
    const std::vector<EventRecord> records =
        readEvents(workspace.path / "events.ndjson");

    std::vector<std::size_t> canonical;
    std::vector<std::size_t> rejected_records;
    for (std::size_t index = 0; index < records.size(); ++index) {
      if (records[index].parsed) {
        canonical.push_back(index);
      } else {
        rejected_records.push_back(index);
      }
    }
    if (rejected_records.size() != 1) {
      throw std::runtime_error(
          "control scenario must contain exactly one truncated record");
    }
    if (canonical.empty()) {
      throw std::runtime_error("scenario contains no parseable updates");
    }

    const std::vector<std::pair<std::string, int>> reordered_keys = {
        {"evt-A-v3", 0}, {"evt-C-v2", 0}, {"evt-A-v2", 0},
        {"evt-B-v2", 0}, {"evt-D-bad", 0}, {"evt-B-v2", 1}};
    std::vector<std::size_t> reordered;
    for (const auto& key : reordered_keys) {
      reordered.push_back(findEvent(records, key.first, key.second));
    }

    const ExecutedRun canonical_run =
        executeRun("canonical", v1, records, canonical, options.output_dir);
    const ExecutedRun reordered_run =
        executeRun("reordered", v1, records, reordered, options.output_dir);
    const std::size_t duplicate_index = findEvent(records, "evt-B-v2", 0);
    const ExecutedRun duplicate_run =
        executeRun("duplicate_submission",
                   snapshotFromModels(canonical_run.final_models), records,
                   {duplicate_index}, options.output_dir,
                   &canonical_run.seen_event_ids);
    const ExecutedRun truncated_run =
        executeTruncatedControl(records.at(rejected_records.front()), v1,
                                options.output_dir);

    const std::string exception_object =
        records.at(findEvent(records, "evt-D-bad", 0)).update.object_id;
    RenderOptions render_options;
    render_options.frames = options.frames;
    render_options.visible = options.visible;
    render_options.screenshot_path =
        (options.output_dir / "vulkan_final_frame.bmp").string();
    RenderResult render;
    if (options.render == "none") {
      render.attempted = false;
      render.skipped = true;
      render.reason = "disabled by --render none";
    } else {
      render = renderModels(canonical_run.final_models,
                            std::set<std::string>{exception_object},
                            render_options);
    }

    const bool v1_d_unchanged =
        modelEqual(v1.models.at(exception_object),
                   canonical_run.final_models.at(exception_object));
    const bool applied_after_exception =
        canonical_run.first_exception_step > 0 &&
        canonical_run.processed_after_exception >= 2 &&
        reordered_run.processed_after_exception >= 1;
    const bool duplicate_rejected =
        duplicate_run.duplicate == 1 && duplicate_run.accepted == 0 &&
        duplicate_run.fingerprint == canonical_run.fingerprint;
    const bool truncated_isolated =
        truncated_run.rejected == 1 && truncated_run.accepted == 0 &&
        truncated_run.fingerprint == sha256Hex(modelsCanonicalText(v1.models));

    picojson::array checks;
    checks.push_back(checkJson(
        "two_version_snapshots",
        v1.snapshot_version == 1 && v2.snapshot_version == 2 &&
            !v1.models.empty() && !v2.models.empty(),
        "loaded snapshot v1 and v2 with " + std::to_string(v1.models.size()) +
            " and " + std::to_string(v2.models.size()) + " models"));
    checks.push_back(checkJson(
        "canonical_matches_v2",
        modelsMatchSnapshot(canonical_run.final_models, v2),
        "canonical fingerprint " + canonical_run.fingerprint));
    checks.push_back(checkJson(
        "reordered_matches_v2",
        modelsMatchSnapshot(reordered_run.final_models, v2),
        "reordered fingerprint " + reordered_run.fingerprint));
    checks.push_back(checkJson(
        "order_independent_convergence",
        canonical_run.fingerprint == reordered_run.fingerprint,
        "canonical and reordered final fingerprints are equal; stale version "
        "was retained without rollback"));
    checks.push_back(checkJson(
        "duplicate_submission_ignored", duplicate_rejected,
        "duplicate count=" + std::to_string(duplicate_run.duplicate) +
            ", accepted=" + std::to_string(duplicate_run.accepted)));
    checks.push_back(checkJson(
        "truncated_record_control", truncated_isolated,
        "line " + std::to_string(records.at(rejected_records.front()).line) +
            " rejected; state fingerprint equals snapshot v1"));
    checks.push_back(checkJson(
        "exception_isolation_and_continuation",
        canonical_run.exception == 1 && applied_after_exception &&
            v1_d_unchanged,
        "invalid bounds for " + exception_object +
            " produced one exception; later outcomes were still processed; "
            "the object kept v1 state"));

    const bool render_required = options.render == "on";
    if (render_required) {
      checks.push_back(checkJson(
          "vulkan_glfw_render", render.ok,
          render.ok ? "adapter=" + render.adapter + "; " + render.reason
                    : render.reason));
    }

    const bool temp_cleaned = workspace.cleanup();
    const bool temp_absent = !fs::exists(workspace.path);
    picojson::object cleanup;
    cleanup["temp_path"] = picojson::value(workspace.path.string());
    cleanup["removed"] = picojson::value(temp_cleaned);
    cleanup["exists_after"] = picojson::value(!temp_absent);
    cleanup["method"] = picojson::value("std::filesystem::remove_all");
    writeJsonFile((options.output_dir / "cleanup.json").string(),
                  picojson::value(cleanup));

    checks.push_back(checkJson(
        "temporary_state_cleaned", temp_cleaned && temp_absent,
        "temporary replay directory removed at end of run"));

    bool passed = true;
    for (const auto& check : checks) {
      passed = passed && check.get<picojson::object>().at("passed").get<bool>();
    }

    picojson::object tech;
    tech["language"] = picojson::value("C++17");
    tech["graphics_api"] = picojson::value("Vulkan 1.0");
    tech["window_system"] = picojson::value("GLFW 3.3+");

    picojson::object snapshots;
    snapshots["v1"] = input_v1;
    snapshots["v2"] = input_v2;
    picojson::object inputs;
    inputs["snapshots"] = picojson::value(snapshots);
    inputs["events"] = input_events;
    inputs["truncated_control"] =
        picojson::value(records.at(rejected_records.front()).raw);

    picojson::object runs;
    runs["canonical"] = runJson(canonical_run);
    runs["reordered"] = runJson(reordered_run);
    runs["duplicate_submission"] = runJson(duplicate_run);
    runs["truncated_control"] = runJson(truncated_run);

    picojson::object verification;
    verification["order_changed"] = picojson::value(true);
    verification["duplicate_event_id"] = picojson::value("evt-B-v2");
    verification["exception_object"] = picojson::value(exception_object);
    verification["continued_after_exception"] =
        picojson::value(applied_after_exception);
    verification["exception_object_stayed_at_v1"] =
        picojson::value(v1_d_unchanged);

    picojson::object summary;
    summary["experiment"] = picojson::value("model-bounding-box-update-replay");
    summary["tech"] = picojson::value(tech);
    summary["inputs"] = picojson::value(inputs);
    summary["runs"] = picojson::value(runs);
    summary["verification"] = picojson::value(verification);
    summary["render"] = renderResultJson(render);
    summary["temp_state"] = picojson::value(cleanup);
    summary["checks"] = picojson::value(checks);
    summary["passed"] = picojson::value(passed);
    writeJsonFile((options.output_dir / "run_summary.json").string(),
                  picojson::value(summary));
    writeManifest(options.output_dir);

    std::cout << "experiment: model-bounding-box-update-replay\n"
              << "canonical fingerprint: " << canonical_run.fingerprint << '\n'
              << "reordered fingerprint: " << reordered_run.fingerprint << '\n'
              << "duplicate fingerprint: " << duplicate_run.fingerprint << '\n'
              << "truncated fingerprint: " << truncated_run.fingerprint << '\n'
              << "vulkan: " << (render.ok ? "ok" : render.reason) << '\n'
              << "temp cleaned: " << (temp_cleaned ? "yes" : "no") << '\n'
              << "evidence: " << options.output_dir << '\n'
              << "result: " << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
  } catch (const std::exception& exception) {
    std::cerr << "error: " << exception.what() << '\n';
    return 1;
  }
}

namespace bbox {
namespace {

ExecutedRun executeRun(const std::string& name, const Snapshot& start,
                       const std::vector<EventRecord>& records,
                       const std::vector<std::size_t>& order,
                       const fs::path& output_dir,
                       const std::unordered_set<std::string>* prior_seen) {
  RunEvidence evidence((output_dir / (name + "_steps.ndjson")).string(), name);
  ReplayEngine engine(start);
  if (prior_seen != nullptr) {
    engine.restoreSeenEventIds(*prior_seen);
  }
  for (std::size_t index = 0; index < order.size(); ++index) {
    const EventRecord& record = records.at(order[index]);
    if (!record.parsed) {
      throw std::runtime_error("executeRun received an unparsed record");
    }
    const StateView before = captureState(engine);
    const ApplyOutcome outcome = engine.apply(record.update);
    const StateView after = captureState(engine);
    evidence.logStep(index + 1, record.line, updateJson(record.update), before,
                     outcome, after);
  }
  const StateView final_state = captureState(engine);
  writeTextFile((output_dir / (name + "_final_state.json")).string(),
                prettyJson(final_state.models));

  ExecutedRun run;
  run.name = name;
  run.log_path = (output_dir / (name + "_steps.ndjson")).string();
  run.final_models = engine.models();
  run.fingerprint = final_state.fingerprint;
  run.accepted = evidence.acceptedCount();
  run.duplicate = evidence.duplicateCount();
  run.stale = evidence.staleCount();
  run.exception = evidence.exceptionCount();
  run.rejected = evidence.rejectedCount();
  run.processed_after_exception = evidence.postExceptionSuccesses();
  run.first_exception_step = evidence.firstExceptionStep();
  run.seen_event_ids = engine.seenEventIds();
  return run;
}

ExecutedRun executeTruncatedControl(const EventRecord& record,
                                    const Snapshot& start,
                                    const fs::path& output_dir) {
  const std::string name = "truncated_control";
  RunEvidence evidence((output_dir / (name + "_steps.ndjson")).string(), name);
  ReplayEngine engine(start);
  const StateView before = captureState(engine);
  picojson::object input;
  input["raw"] = picojson::value(record.raw);
  input["source_line"] = picojson::value(static_cast<double>(record.line));
  evidence.logRejected(1, record.line, picojson::value(input),
                       "truncated JSON rejected before state mutation: " +
                           record.error,
                       before);
  const StateView after = captureState(engine);
  writeTextFile((output_dir / (name + "_final_state.json")).string(),
                prettyJson(after.models));

  ExecutedRun run;
  run.name = name;
  run.log_path = (output_dir / (name + "_steps.ndjson")).string();
  run.final_models = engine.models();
  run.fingerprint = after.fingerprint;
  run.rejected = evidence.rejectedCount();
  return run;
}

picojson::value runJson(const ExecutedRun& run) {
  picojson::object counters;
  counters["accepted"] = picojson::value(static_cast<double>(run.accepted));
  counters["duplicate"] = picojson::value(static_cast<double>(run.duplicate));
  counters["stale"] = picojson::value(static_cast<double>(run.stale));
  counters["exception"] = picojson::value(static_cast<double>(run.exception));
  counters["rejected"] = picojson::value(static_cast<double>(run.rejected));

  picojson::object object;
  object["counters"] = picojson::value(counters);
  object["first_exception_step"] =
      picojson::value(static_cast<double>(run.first_exception_step));
  object["processed_after_exception"] =
      picojson::value(static_cast<double>(run.processed_after_exception));
  object["final_fingerprint"] = picojson::value(run.fingerprint);
  object["step_log"] = picojson::value(fs::path(run.log_path).filename().string());
  return picojson::value(object);
}

picojson::value checkJson(const std::string& name, bool passed,
                          const std::string& evidence) {
  picojson::object object;
  object["name"] = picojson::value(name);
  object["passed"] = picojson::value(passed);
  object["evidence"] = picojson::value(evidence);
  return picojson::value(object);
}

Snapshot snapshotFromModels(const std::map<std::string, Model>& models) {
  Snapshot snapshot;
  snapshot.snapshot_version = 0;
  snapshot.captured_at = "runtime";
  snapshot.models = models;
  return snapshot;
}

bool modelEqual(const Model& left, const Model& right) {
  return left.version == right.version &&
         left.last_event == right.last_event &&
         left.bounds.equals(right.bounds);
}

bool modelsMatchSnapshot(const std::map<std::string, Model>& models,
                         const Snapshot& expected) {
  if (models.size() != expected.models.size()) {
    return false;
  }
  for (const auto& entry : expected.models) {
    const auto it = models.find(entry.first);
    if (it == models.end() || !modelEqual(it->second, entry.second)) {
      return false;
    }
  }
  return true;
}

void writeManifest(const fs::path& output_dir) {
  std::vector<fs::path> paths;
  for (const auto& entry : fs::recursive_directory_iterator(output_dir)) {
    if (entry.is_regular_file() &&
        entry.path().filename() != "evidence_manifest.json") {
      paths.push_back(entry.path());
    }
  }
  std::sort(paths.begin(), paths.end());
  picojson::array files;
  for (const fs::path& path : paths) {
    picojson::object file;
    file["path"] = picojson::value(
        fs::relative(path, output_dir).generic_string());
    file["sha256"] = picojson::value(sha256File(path.string()));
    file["bytes"] = picojson::value(
        static_cast<double>(fs::file_size(path)));
    files.push_back(picojson::value(file));
  }
  picojson::object manifest;
  manifest["algorithm"] = picojson::value("SHA-256");
  manifest["files"] = picojson::value(files);
  writeJsonFile((output_dir / "evidence_manifest.json").string(),
                picojson::value(manifest));
}

}  // namespace
}  // namespace bbox
