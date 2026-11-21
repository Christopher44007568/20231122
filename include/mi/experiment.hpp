#pragma once

#include <string>
#include <vector>

#include "mi/backend.hpp"
#include "mi/scenarios.hpp"
#include "mi/types.hpp"

namespace mi {

struct ExperimentResult {
  std::vector<ScenarioResult> scenarios;
  ReplayResult replay;
  std::vector<StepRecord> all_steps;
  StateSnapshot final_state;
  bool passed{false};
};

std::string serialize_command(const Command& command);
bool parse_command(const std::string& line, Command* command, std::string* error);

ExperimentResult execute_experiment(GpuBackend* gpu,
                                    const std::vector<Scenario>& scenarios);
ReplayResult replay_commands(GpuBackend* gpu,
                             const std::vector<Command>& commands,
                             const std::vector<StepRecord>& original);

std::string serialize_event(const StepRecord& step);
std::string serialize_commands(const std::vector<Command>& commands);
std::string serialize_state_json(const StateSnapshot& state);
std::string serialize_checks_json(const ExperimentResult& result);
std::vector<Command> flatten_commands(const std::vector<Scenario>& scenarios);
std::vector<StepRecord> run_command_stream(GpuBackend* gpu,
                                           const std::vector<Command>& commands);

}  // namespace mi
