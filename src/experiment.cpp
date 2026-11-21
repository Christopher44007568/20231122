#include "mi/experiment.hpp"

#include <algorithm>
#include <sstream>

#include "mi/json.hpp"
#include "mi/sha256.hpp"
#include "mi/state_machine.hpp"

namespace mi {
namespace {

constexpr const char* kChainSeed = "mi-hotupdate-chain/v1";

std::string input_json(const Command& command) {
  std::ostringstream out;
  out << "{\"material\":" << json_string(command.material)
      << ",\"key\":" << json_string(command.key)
      << ",\"tx\":" << command.tx
      << ",\"version\":" << command.version
      << ",\"color_r\":" << json_number(command.params.base_color[0])
      << ",\"color_g\":" << json_number(command.params.base_color[1])
      << ",\"color_b\":" << json_number(command.params.base_color[2])
      << ",\"color_a\":" << json_number(command.params.base_color[3])
      << ",\"roughness\":" << json_number(command.params.roughness)
      << ",\"metallic\":" << json_number(command.params.metallic)
      << ",\"shader_variant\":" << command.params.shader_variant
      << "}";
  return out.str();
}

std::string snapshot_json(const StateSnapshot& state) {
  std::ostringstream out;
  out << "{\"material\":" << json_string(state.material)
      << ",\"active_version\":" << state.active_version
      << ",\"active_payload_hash\":" << json_string(state.active_payload_hash)
      << ",\"pending_tx\":" << state.pending_tx
      << ",\"pending_stage\":" << json_string(state.pending_stage)
      << ",\"pending_gpu_resident\":"
      << json_bool(state.pending_gpu_resident)
      << ",\"transaction_count\":" << state.transaction_count
      << ",\"transaction_history_count\":" << state.transaction_history_count
      << ",\"commit_count\":" << state.commit_count
      << ",\"cancel_count\":" << state.cancel_count
      << ",\"duplicate_count\":" << state.duplicate_count
      << ",\"rejected_count\":" << state.rejected_count
      << "}";
  return out.str();
}

std::string result_json(const Outcome& result) {
  std::ostringstream out;
  out << "{\"status\":" << json_string(result.status)
      << ",\"code\":" << json_string(result.code)
      << ",\"detail\":" << json_string(result.detail)
      << ",\"tx\":" << result.tx
      << ",\"active_changed\":" << json_bool(result.active_changed)
      << ",\"duplicate\":" << json_bool(result.duplicate)
      << ",\"readback_verified\":"
      << json_bool(result.gpu_readback_verified)
      << ",\"gpu_released\":" << json_bool(result.gpu_released)
      << "}";
  return out.str();
}

std::string evidence_json(const Evidence& evidence) {
  std::ostringstream out;
  out << "{\"request_hash\":" << json_string(evidence.request_hash)
      << ",\"payload_hash\":" << json_string(evidence.payload_hash)
      << ",\"gpu_payload_id\":" << json_string(evidence.gpu_payload_id)
      << ",\"gpu_readback_verified\":"
      << json_bool(evidence.gpu_readback_verified)
      << ",\"gpu_released\":" << json_bool(evidence.gpu_released)
      << ",\"prior_active_hash\":" << json_string(evidence.prior_active_hash)
      << ",\"new_active_hash\":" << json_string(evidence.new_active_hash)
      << "}";
  return out.str();
}

std::string build_core(std::uint64_t seq, const Command& command,
                       const StateSnapshot& before, const StateSnapshot& after,
                       const Outcome& outcome, const Evidence& evidence) {
  std::ostringstream out;
  out << "{\"seq\":" << seq
      << ",\"scenario\":" << json_string(command.scenario)
      << ",\"scenario_seq\":" << command.scenario_seq
      << ",\"op\":" << json_string(to_string(command.op))
      << ",\"input\":" << input_json(command)
      << ",\"state_before\":" << snapshot_json(before)
      << ",\"result\":" << result_json(outcome)
      << ",\"state_after\":" << snapshot_json(after)
      << ",\"evidence\":" << evidence_json(evidence)
      << "}";
  return out.str();
}

Evidence evidence_from_outcome(const Outcome& outcome) {
  Evidence evidence;
  evidence.request_hash = outcome.request_hash;
  evidence.payload_hash = outcome.payload_hash;
  evidence.gpu_payload_id = outcome.gpu_payload_id;
  evidence.gpu_readback_verified = outcome.gpu_readback_verified;
  evidence.gpu_released = outcome.gpu_released;
  evidence.prior_active_hash = outcome.prior_active_hash;
  evidence.new_active_hash = outcome.new_active_hash;
  return evidence;
}

StepRecord make_record(MaterialHotUpdater* updater, const Command& command,
                       std::uint64_t seq, const std::string& prev_hash) {
  StepRecord record;
  record.global_seq = seq;
  record.command = command;
  record.before = updater->snapshot();
  record.outcome = updater->execute(command);
  record.after = updater->snapshot();
  record.evidence = evidence_from_outcome(record.outcome);
  record.prev_hash = prev_hash;
  record.core_json = build_core(seq, command, record.before, record.after,
                                record.outcome, record.evidence);
  record.event_hash = sha256_hex(prev_hash + "\n" + record.core_json);
  return record;
}

std::string next_hash(const StepRecord& record) { return record.event_hash; }

void add_check(ScenarioResult* result, const std::string& id, bool ok,
               const std::string& detail) {
  result->checks.push_back(Check{id, ok, detail});
}

const StepRecord* find_op(const ScenarioResult& result, Op op,
                          std::size_t occurrence = 0) {
  for (const auto& step : result.steps) {
    if (step.command.op == op) {
      if (occurrence == 0) {
        return &step;
      }
      --occurrence;
    }
  }
  return nullptr;
}

}  // namespace

std::string serialize_command(const Command& command) {
  std::ostringstream out;
  out << "{\"schema\":\"mi-hotupdate-command/v1\""
      << ",\"scenario\":" << json_string(command.scenario)
      << ",\"scenario_seq\":" << command.scenario_seq
      << ",\"op\":" << json_string(to_string(command.op))
      << ",\"material\":" << json_string(command.material)
      << ",\"key\":" << json_string(command.key)
      << ",\"tx\":" << command.tx
      << ",\"version\":" << command.version
      << ",\"color_r\":" << json_number(command.params.base_color[0])
      << ",\"color_g\":" << json_number(command.params.base_color[1])
      << ",\"color_b\":" << json_number(command.params.base_color[2])
      << ",\"color_a\":" << json_number(command.params.base_color[3])
      << ",\"roughness\":" << json_number(command.params.roughness)
      << ",\"metallic\":" << json_number(command.params.metallic)
      << ",\"shader_variant\":" << command.params.shader_variant
      << ",\"expected\":" << json_string(command.expected)
      << "}";
  return out.str();
}

bool parse_command(const std::string& line, Command* command, std::string* error) {
  if (command == nullptr) {
    return false;
  }
  if (get_json_string(line, "schema") != "mi-hotupdate-command/v1") {
    if (error != nullptr) {
      *error = "unsupported or missing command schema";
    }
    return false;
  }
  const std::string op = get_json_string(line, "op");
  if (op == "reset") {
    command->op = Op::Reset;
  } else if (op == "submit") {
    command->op = Op::Submit;
  } else if (op == "validate") {
    command->op = Op::Validate;
  } else if (op == "upload_begin") {
    command->op = Op::UploadBegin;
  } else if (op == "upload_complete") {
    command->op = Op::UploadComplete;
  } else if (op == "commit") {
    command->op = Op::Commit;
  } else if (op == "cancel") {
    command->op = Op::Cancel;
  } else {
    if (error != nullptr) {
      *error = "unknown op: " + op;
    }
    return false;
  }
  command->scenario = get_json_string(line, "scenario", "unknown");
  command->scenario_seq = get_json_u64(line, "scenario_seq");
  command->material = get_json_string(line, "material");
  command->key = get_json_string(line, "key");
  command->tx = get_json_u64(line, "tx");
  command->version = get_json_u64(line, "version");
  command->params.base_color = {
      static_cast<float>(get_json_double(line, "color_r")),
      static_cast<float>(get_json_double(line, "color_g")),
      static_cast<float>(get_json_double(line, "color_b")),
      static_cast<float>(get_json_double(line, "color_a"))};
  command->params.roughness =
      static_cast<float>(get_json_double(line, "roughness"));
  command->params.metallic =
      static_cast<float>(get_json_double(line, "metallic"));
  command->params.shader_variant =
      static_cast<std::uint32_t>(get_json_u64(line, "shader_variant"));
  command->expected = get_json_string(line, "expected");
  return true;
}

std::string serialize_event(const StepRecord& step) {
  std::string core = step.core_json;
  if (core.empty() || core.back() != '}') {
    return core;
  }
  core.pop_back();
  std::ostringstream out;
  out << core
      << ",\"expected\":" << json_string(step.command.expected)
      << ",\"status_match\":"
      << json_bool(step.outcome.status == step.command.expected)
      << ",\"prev_hash\":" << json_string(step.prev_hash)
      << ",\"event_hash\":" << json_string(step.event_hash)
      << "}";
  return out.str();
}

std::vector<Command> flatten_commands(const std::vector<Scenario>& scenarios) {
  std::vector<Command> commands;
  for (const auto& scenario : scenarios) {
    commands.insert(commands.end(), scenario.commands.begin(),
                    scenario.commands.end());
  }
  return commands;
}

std::string serialize_commands(const std::vector<Command>& commands) {
  std::ostringstream out;
  for (const auto& command : commands) {
    out << serialize_command(command) << '\n';
  }
  return out.str();
}

std::string serialize_state_json(const StateSnapshot& state) {
  return snapshot_json(state);
}

std::string serialize_checks_json(const ExperimentResult& result) {
  std::ostringstream out;
  out << "{\"passed\":" << json_bool(result.passed) << ",\"scenarios\":[";
  bool first_scenario = true;
  for (const auto& scenario : result.scenarios) {
    if (!first_scenario) {
      out << ",";
    }
    first_scenario = false;
    out << "{\"name\":" << json_string(scenario.name)
        << ",\"passed\":" << json_bool(scenario.passed)
        << ",\"checks\":[";
    bool first_check = true;
    for (const auto& check : scenario.checks) {
      if (!first_check) {
        out << ",";
      }
      first_check = false;
      out << "{\"id\":" << json_string(check.id)
          << ",\"ok\":" << json_bool(check.ok)
          << ",\"detail\":" << json_string(check.detail) << "}";
    }
    out << "]}";
  }
  out << "]}";
  return out.str();
}

std::vector<StepRecord> run_command_stream(GpuBackend* gpu,
                                           const std::vector<Command>& commands) {
  MaterialHotUpdater updater(gpu);
  std::vector<StepRecord> records;
  std::string previous = sha256_hex(kChainSeed);
  std::uint64_t sequence = 0;
  for (const auto& command : commands) {
    StepRecord record = make_record(&updater, command, sequence++, previous);
    previous = record.event_hash;
    records.push_back(std::move(record));
  }
  return records;
}

ExperimentResult execute_experiment(GpuBackend* gpu,
                                    const std::vector<Scenario>& scenarios) {
  ExperimentResult result;
  MaterialHotUpdater updater(gpu);
  std::string previous = sha256_hex(kChainSeed);
  std::uint64_t global_seq = 0;

  for (const auto& scenario : scenarios) {
    ScenarioResult scenario_result;
    scenario_result.name = scenario.name;
    scenario_result.purpose = scenario.purpose;
    scenario_result.control_of = scenario.control_of;

    for (const auto& command : scenario.commands) {
      StepRecord step = make_record(&updater, command, global_seq++, previous);
      previous = next_hash(step);
      result.all_steps.push_back(step);
      scenario_result.steps.push_back(step);
    }

    bool statuses_ok = true;
    std::size_t matching = 0;
    for (const auto& step : scenario_result.steps) {
      if (step.outcome.status == step.command.expected) {
        ++matching;
      } else {
        statuses_ok = false;
      }
    }
    add_check(&scenario_result, "expected_statuses", statuses_ok,
              std::to_string(matching) + "/" +
                  std::to_string(scenario_result.steps.size()) +
                  " statuses matched");

    const StateSnapshot& final_state = scenario_result.steps.back().after;
    if (scenario.name == "cancel_primary") {
      const StepRecord* upload = find_op(scenario_result, Op::UploadBegin);
      const StepRecord* cancel = find_op(scenario_result, Op::Cancel);
      const StepRecord* commit = find_op(scenario_result, Op::Commit);
      add_check(&scenario_result, "cancel_saw_staged_gpu",
                upload != nullptr && cancel != nullptr &&
                    cancel->before.pending_stage == "uploading" &&
                    cancel->before.pending_gpu_resident,
                "cancel occurred after upload_begin while staged GPU resource was resident");
      add_check(&scenario_result, "cancel_released_gpu",
                cancel != nullptr && cancel->evidence.gpu_released &&
                    cancel->after.pending_tx == 0,
                "transaction GPU resource was released and pending cleared");
      add_check(&scenario_result, "active_not_changed_by_cancel",
                final_state.active_version == 1 &&
                    final_state.commit_count == 0,
                "active version stayed at 1 with zero commits");
      add_check(&scenario_result, "late_commit_rejected",
                commit != nullptr &&
                    commit->outcome.status == "invalid_transition",
                "commit after cancellation was rejected");
    } else if (scenario.name == "cancel_control") {
      const StepRecord* first_cancel = find_op(scenario_result, Op::Cancel, 0);
      const StepRecord* second_cancel = find_op(scenario_result, Op::Cancel, 1);
      const StepRecord* duplicate = find_op(scenario_result, Op::Submit, 1);
      add_check(&scenario_result, "duplicate_reused_tx",
                duplicate != nullptr &&
                    duplicate->outcome.status == "duplicate_accepted" &&
                    duplicate->outcome.tx == 1,
                "duplicate submission reused transaction 1");
      add_check(&scenario_result, "repeated_cancel_noop",
                first_cancel != nullptr && second_cancel != nullptr &&
                    first_cancel->outcome.status == "canceled" &&
                    second_cancel->outcome.status == "already_canceled" &&
                    final_state.cancel_count == 1,
                "the second cancellation was a no-op");
      add_check(&scenario_result, "control_rolled_back",
                final_state.active_version == 1 &&
                    final_state.commit_count == 0,
                "control run also retained active version 1");
    } else if (scenario.name == "no_cancel_baseline") {
      const StepRecord* commit = find_op(scenario_result, Op::Commit);
      const StepRecord* late_cancel = find_op(scenario_result, Op::Cancel);
      add_check(&scenario_result, "baseline_committed",
                final_state.active_version == 2 &&
                    final_state.commit_count == 1 &&
                    final_state.pending_tx == 0,
                "uncancelled update committed exactly once");
      add_check(&scenario_result, "gpu_readback_matched",
                commit != nullptr &&
                    commit->evidence.gpu_readback_verified &&
                    commit->evidence.new_active_hash ==
                        commit->evidence.payload_hash,
                "GPU readback hash matched the update payload");
      add_check(&scenario_result, "cancel_after_commit_rejected",
                late_cancel != nullptr &&
                    late_cancel->outcome.status == "too_late" &&
                    late_cancel->before.active_version == 2 &&
                    late_cancel->after.active_version == 2,
                "late cancellation was rejected without rolling back v2");
    } else if (scenario.name == "reordered_events") {
      const StepRecord* early_upload =
          find_op(scenario_result, Op::UploadBegin, 0);
      const StepRecord* early_commit =
          find_op(scenario_result, Op::Commit, 0);
      add_check(&scenario_result, "upload_before_validate_rejected",
                early_upload != nullptr &&
                    early_upload->outcome.status == "invalid_transition" &&
                    early_upload->before.pending_stage == "submitted" &&
                    early_upload->after.pending_stage == "submitted",
                "out-of-order upload did not change the stage");
      add_check(&scenario_result, "commit_before_ready_rejected",
                early_commit != nullptr &&
                    early_commit->outcome.status == "invalid_transition" &&
                    early_commit->after.active_version == 1,
                "premature commit did not change active state");
      add_check(&scenario_result, "valid_order_recovered",
                final_state.active_version == 2 &&
                    final_state.commit_count == 1,
                "a correctly ordered retry completed the update");
    } else if (scenario.name == "duplicate_submit") {
      const StepRecord* first = find_op(scenario_result, Op::Submit, 0);
      const StepRecord* duplicate = find_op(scenario_result, Op::Submit, 1);
      const StepRecord* conflict = find_op(scenario_result, Op::Submit, 2);
      add_check(&scenario_result, "duplicate_same_tx",
                first != nullptr && duplicate != nullptr &&
                    first->outcome.tx == duplicate->outcome.tx &&
                    duplicate->outcome.duplicate,
                "same key and payload mapped to one transaction");
      add_check(&scenario_result, "conflicting_payload_rejected",
                conflict != nullptr && first != nullptr &&
                    conflict->outcome.status == "idempotency_conflict" &&
                    conflict->outcome.tx == first->outcome.tx,
                "same key with different payload was rejected");
      add_check(&scenario_result, "one_version_increment",
                final_state.active_version == 2 &&
                    final_state.commit_count == 1 &&
                    final_state.transaction_history_count == 1,
                "all retries produced one transaction and one commit");
    }

    scenario_result.passed =
        std::all_of(scenario_result.checks.begin(), scenario_result.checks.end(),
                    [](const Check& check) { return check.ok; });
    result.scenarios.push_back(std::move(scenario_result));
  }

  const ScenarioResult* primary = nullptr;
  ScenarioResult* control = nullptr;
  for (auto& scenario : result.scenarios) {
    if (scenario.name == "cancel_primary") {
      primary = &scenario;
    }
    if (scenario.name == "cancel_control") {
      control = &scenario;
    }
  }
  if (primary != nullptr && control != nullptr) {
    const auto& primary_end = primary->steps.back().after;
    const auto& control_end = control->steps.back().after;
    add_check(control, "control_matches_primary",
              primary_end.active_version == control_end.active_version &&
                  primary_end.active_payload_hash ==
                      control_end.active_payload_hash &&
                  primary_end.commit_count == control_end.commit_count,
              "both cancellation runs converged to the same active state");
    control->passed =
        std::all_of(control->checks.begin(), control->checks.end(),
                    [](const Check& check) { return check.ok; });
  }

  result.final_state = result.all_steps.back().after;
  result.replay = replay_commands(gpu, flatten_commands(scenarios),
                                  result.all_steps);
  result.passed =
      result.replay.ok &&
      std::all_of(result.scenarios.begin(), result.scenarios.end(),
                  [](const ScenarioResult& scenario) {
                    return scenario.passed;
                  });
  return result;
}

ReplayResult replay_commands(GpuBackend* gpu,
                             const std::vector<Command>& commands,
                             const std::vector<StepRecord>& original) {
  ReplayResult result;
  result.original_steps = original.size();
  MaterialHotUpdater updater(gpu);
  std::string previous = sha256_hex(kChainSeed);
  std::uint64_t seq = 0;

  for (const auto& command : commands) {
    StepRecord replay = make_record(&updater, command, seq, previous);
    previous = replay.event_hash;
    if (seq >= original.size()) {
      result.first_mismatch = "replay produced extra step " + std::to_string(seq);
      result.replayed_steps = seq + 1;
      break;
    }
    if (original[seq].core_json != replay.core_json ||
        original[seq].event_hash != replay.event_hash) {
      result.first_mismatch =
          "step " + std::to_string(seq) + " differs (" +
          original[seq].event_hash.substr(0, 12) + " != " +
          replay.event_hash.substr(0, 12) + ")";
      result.replayed_steps = seq + 1;
      break;
    }
    ++seq;
  }
  if (result.first_mismatch.empty()) {
    result.replayed_steps = seq;
  }
  if (result.first_mismatch.empty() && seq != original.size()) {
    result.first_mismatch = "replay ended before all original steps";
  }
  if (!original.empty()) {
    result.original_final_hash = original.back().event_hash;
  }
  result.replayed_final_hash = previous;
  result.ok = result.first_mismatch.empty() && seq == original.size() &&
              result.original_final_hash == result.replayed_final_hash;
  return result;
}

}  // namespace mi
