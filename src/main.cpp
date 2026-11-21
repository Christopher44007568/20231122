#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mi/backend.hpp"
#include "mi/experiment.hpp"
#include "mi/json.hpp"
#include "mi/report.hpp"
#include "mi/scenarios.hpp"
#include "mi/sha256.hpp"
#include "mi/vulkan_backend.hpp"

namespace {

struct Options {
  std::string output{"artifacts/latest"};
  std::string backend{"vulkan"};
  std::string replay_file;
  bool visible{false};
};

Options parse_options(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto require_value = [&](const char* name) {
      if (i + 1 >= argc) {
        throw std::runtime_error(std::string("missing value for ") + name);
      }
      return std::string{argv[++i]};
    };
    if (arg == "--output") {
      options.output = require_value("--output");
    } else if (arg == "--backend") {
      options.backend = require_value("--backend");
    } else if (arg == "--replay") {
      options.replay_file = require_value("--replay");
    } else if (arg == "--visible") {
      options.visible = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout
          << "Usage: material_hotupdate [--output DIR] "
             "[--backend vulkan|fake] [--visible] [--replay FILE]\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (options.backend != "vulkan" && options.backend != "fake") {
    throw std::runtime_error("--backend must be vulkan or fake");
  }
  return options;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot read " + path.string());
  }
  std::ostringstream data;
  data << input.rdbuf();
  return data.str();
}

void write_file(const std::filesystem::path& path, const std::string& data) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("cannot write " + path.string());
  }
  output << data;
  if (!output) {
    throw std::runtime_error("write failed for " + path.string());
  }
}

std::vector<mi::Command> read_commands(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot read replay file " + path.string());
  }
  std::vector<mi::Command> commands;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }
    mi::Command command;
    std::string error;
    if (!mi::parse_command(line, &command, &error)) {
      throw std::runtime_error("replay line " + std::to_string(line_number) +
                               ": " + error);
    }
    commands.push_back(std::move(command));
  }
  return commands;
}

std::unique_ptr<mi::GpuBackend> make_backend(const Options& options) {
  if (options.backend == "fake") {
    return std::make_unique<mi::FakeGpuBackend>();
  }
  return mi::create_vulkan_backend(options.visible);
}

std::string environment_json(const mi::BackendInfo& backend,
                             const std::string& output,
                             const std::string& failure = {}) {
  std::ostringstream out;
  out << "{\"schema\":\"mi-hotupdate-environment/v1\""
      << ",\"language\":\"C++17\""
      << ",\"cpp_standard\":" << __cplusplus
      << ",\"compiler\":" << mi::json_string(__VERSION__)
      << ",\"backend_name\":" << mi::json_string(backend.name)
      << ",\"backend_device\":" << mi::json_string(backend.device)
      << ",\"backend_driver\":" << mi::json_string(backend.driver)
      << ",\"backend_api\":" << mi::json_string(backend.api_version)
      << ",\"asynchronous_submit\":"
      << mi::json_bool(backend.asynchronous_submit)
      << ",\"output\":" << mi::json_string(output)
      << ",\"failure\":" << mi::json_string(failure)
      << "}";
  return out.str();
}

std::string replay_json(const mi::ReplayResult& replay) {
  std::ostringstream out;
  out << "{\"ok\":" << mi::json_bool(replay.ok)
      << ",\"original_steps\":" << replay.original_steps
      << ",\"replayed_steps\":" << replay.replayed_steps
      << ",\"first_mismatch\":" << mi::json_string(replay.first_mismatch)
      << ",\"original_final_hash\":"
      << mi::json_string(replay.original_final_hash)
      << ",\"replayed_final_hash\":"
      << mi::json_string(replay.replayed_final_hash)
      << "}";
  return out.str();
}

std::string events_text(const std::vector<mi::StepRecord>& records) {
  std::ostringstream out;
  for (const auto& record : records) {
    out << mi::serialize_event(record) << '\n';
  }
  return out.str();
}

std::vector<std::string> read_event_hashes(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::vector<std::string> hashes;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty()) {
      hashes.push_back(mi::get_json_string(line, "event_hash"));
    }
  }
  return hashes;
}

mi::ReplayResult compare_replay(const std::string& events_path,
                                const std::vector<mi::StepRecord>& records) {
  mi::ReplayResult result;
  const std::vector<std::string> original = read_event_hashes(events_path);
  result.original_steps = original.size();
  result.replayed_steps = records.size();
  if (original.empty()) {
    result.first_mismatch = "no original events.jsonl found for hash comparison";
    return result;
  }
  if (!records.empty()) {
    result.replayed_final_hash = records.back().event_hash;
  }
  result.original_final_hash = original.back();
  const std::size_t common = std::min(original.size(), records.size());
  for (std::size_t i = 0; i < common; ++i) {
    if (original[i] != records[i].event_hash) {
      result.first_mismatch = "step " + std::to_string(i) + " hash mismatch";
      return result;
    }
  }
  if (original.size() != records.size()) {
    result.first_mismatch = "step count differs";
    return result;
  }
  result.ok = result.original_final_hash == result.replayed_final_hash;
  if (!result.ok) {
    result.first_mismatch = "final hash differs";
  }
  return result;
}

void write_sums(const std::filesystem::path& output_dir,
                const std::vector<std::string>& names) {
  std::ostringstream sums;
  for (const auto& name : names) {
    const std::filesystem::path path = output_dir / name;
    if (!std::filesystem::exists(path)) {
      continue;
    }
    sums << mi::sha256_hex(read_file(path)) << "  " << name << '\n';
  }
  write_file(output_dir / "SHA256SUMS", sums.str());
}

void write_artifacts(const std::filesystem::path& output_dir,
                     const mi::ExperimentResult& result,
                     const std::vector<mi::Command>& commands,
                     const mi::ReportContext& context) {
  write_file(output_dir / "commands.jsonl", mi::serialize_commands(commands));
  write_file(output_dir / "events.jsonl", events_text(result.all_steps));
  write_file(output_dir / "final_state.json",
             mi::serialize_state_json(result.final_state) + "\n");
  write_file(output_dir / "checks.json",
             mi::serialize_checks_json(result) + "\n");
  write_file(output_dir / "replay.json", replay_json(result.replay) + "\n");
  write_file(output_dir / "environment.json",
             environment_json(context.backend, context.output_dir) + "\n");
  write_file(output_dir / "report.md", mi::render_report(result, context));
  write_file(output_dir / "manual_confirmation.md",
             mi::render_manual_confirmation(result, context));
  write_sums(output_dir, {"commands.jsonl", "events.jsonl", "final_state.json",
                          "checks.json", "replay.json", "environment.json",
                          "report.md", "manual_confirmation.md"});
}

int run_experiment(const Options& options,
                   const std::filesystem::path& output_dir) {
  const std::vector<mi::Scenario> scenarios = mi::build_scenarios();
  const std::vector<mi::Command> commands = mi::flatten_commands(scenarios);
  write_file(output_dir / "commands.jsonl", mi::serialize_commands(commands));

  std::unique_ptr<mi::GpuBackend> backend = make_backend(options);
  mi::ExperimentResult result = mi::execute_experiment(backend.get(), scenarios);
  mi::ReportContext context;
  context.backend = backend->info();
  context.output_dir = output_dir.string();
  context.build_mode = options.backend;
  write_artifacts(output_dir, result, commands, context);

  std::cout << (result.passed ? "PASS" : "FAIL")
            << " steps=" << result.all_steps.size()
            << " replay=" << (result.replay.ok ? "PASS" : "FAIL")
            << " report=" << (output_dir / "report.md").string() << '\n';
  return result.passed ? 0 : 1;
}

int run_replay(const Options& options,
               const std::filesystem::path& output_dir) {
  const std::filesystem::path replay_path = options.replay_file;
  const std::vector<mi::Command> commands = read_commands(replay_path);
  write_file(output_dir / "commands.jsonl", mi::serialize_commands(commands));

  std::unique_ptr<mi::GpuBackend> backend = make_backend(options);
  const std::vector<mi::StepRecord> records =
      mi::run_command_stream(backend.get(), commands);
  mi::ReplayResult comparison =
      compare_replay(replay_path.parent_path() / "events.jsonl", records);

  mi::ExperimentResult result;
  result.all_steps = records;
  result.replay = comparison;
  result.passed = comparison.ok;
  if (!records.empty()) {
    result.final_state = records.back().after;
  }
  mi::ReportContext context;
  context.backend = backend->info();
  context.output_dir = output_dir.string();
  context.build_mode = options.backend;
  write_artifacts(output_dir, result, commands, context);
  write_file(output_dir / "replay_source.txt", replay_path.string() + "\n");

  std::cout << (comparison.ok ? "PASS" : "FAIL")
            << " replay_steps=" << comparison.replayed_steps
            << " original_steps=" << comparison.original_steps
            << " report=" << (output_dir / "report.md").string() << '\n';
  if (!comparison.first_mismatch.empty()) {
    std::cerr << "replay mismatch: " << comparison.first_mismatch << '\n';
  }
  return comparison.ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  try {
    options = parse_options(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "argument error: " << error.what() << '\n';
    return 2;
  }

  const std::filesystem::path output_dir = options.output;
  try {
    std::filesystem::create_directories(output_dir);
    return options.replay_file.empty() ? run_experiment(options, output_dir)
                                       : run_replay(options, output_dir);
  } catch (const std::exception& error) {
    mi::BackendInfo backend;
    backend.name = "unavailable";
    backend.device = "unavailable";
    backend.driver = "unavailable";
    mi::ReportContext context;
    context.backend = backend;
    context.output_dir = output_dir.string();
    context.build_mode = options.backend;
    context.failure = error.what();
    try {
      write_file(output_dir / "failure.txt", context.failure + "\n");
      write_file(output_dir / "environment.json",
                 environment_json(backend, context.output_dir,
                                  context.failure) +
                     "\n");
      write_file(output_dir / "report.md",
                 mi::render_failure_report(context));
      mi::ExperimentResult empty_result;
      write_file(output_dir / "manual_confirmation.md",
                 mi::render_manual_confirmation(empty_result, context));
      write_sums(output_dir,
                 {"failure.txt", "environment.json", "report.md",
                  "manual_confirmation.md"});
    } catch (const std::exception& write_error) {
      std::cerr << "failed to persist failure evidence: "
                << write_error.what() << '\n';
    }
    std::cerr << "experiment failed: " << error.what() << '\n'
              << "see " << (output_dir / "report.md").string() << '\n';
    return 2;
  }
}
