#include <iostream>
#include <string>

#include "mi/backend.hpp"
#include "mi/experiment.hpp"
#include "mi/report.hpp"
#include "mi/scenarios.hpp"
#include "mi/sha256.hpp"
#include "mi/state_machine.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

void test_sha256() {
  check(mi::sha256_hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 known vector");
  check(mi::sha256_hex("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA-256 empty vector");
}

void test_command_roundtrip() {
  const auto scenarios = mi::build_scenarios();
  const auto commands = mi::flatten_commands(scenarios);
  check(!commands.empty(), "scenario commands exist");
  if (commands.empty()) {
    return;
  }
  const std::string line = mi::serialize_command(commands.front());
  mi::Command parsed;
  std::string error;
  check(mi::parse_command(line, &parsed, &error), "command parses: " + error);
  check(parsed.scenario == commands.front().scenario, "scenario roundtrip");
  check(parsed.op == commands.front().op, "op roundtrip");
  check(parsed.params.roughness == commands.front().params.roughness,
        "float parameter roundtrip");
  check(parsed.expected == commands.front().expected, "expected roundtrip");
}

void test_experiment() {
  mi::FakeGpuBackend gpu;
  const auto scenarios = mi::build_scenarios();
  const mi::ExperimentResult result = mi::execute_experiment(&gpu, scenarios);
  check(result.passed, "deterministic experiment passes");
  check(result.replay.ok, "self replay passes");
  check(result.replay.original_steps == result.all_steps.size(),
        "replay covers every step");
  check(result.replay.original_final_hash == result.replay.replayed_final_hash,
        "replay final hashes match");
  check(result.scenarios.size() == 5, "all five scenarios executed");

  for (const auto& scenario : result.scenarios) {
    check(scenario.passed, "scenario passes: " + scenario.name);
    for (const auto& step : scenario.steps) {
      check(step.outcome.status == step.command.expected,
            scenario.name + " step " + std::to_string(step.command.scenario_seq) +
                " expected " + step.command.expected + ", got " +
                step.outcome.status);
    }
  }

  const auto& reordered = result.scenarios[3];
  check(reordered.name == "reordered_events", "reordered scenario position");
  check(reordered.steps[2].outcome.status == "invalid_transition",
        "upload-before-validate rejected");

  const auto& primary = result.scenarios[0].steps;
  check(primary[3].evidence.request_hash.size() == 64,
        "upload evidence contains a request hash");
  check(primary[4].evidence.request_hash == primary[3].evidence.request_hash,
        "cancel evidence preserves the transaction request hash");
  check(primary[4].evidence.payload_hash == primary[3].evidence.payload_hash,
        "cancel evidence preserves the transaction payload hash");
  const mi::StepRecord* baseline_commit = nullptr;
  for (const auto& step : result.scenarios[2].steps) {
    if (step.command.op == mi::Op::Commit) {
      baseline_commit = &step;
    }
  }
  check(baseline_commit != nullptr &&
            baseline_commit->evidence.request_hash.size() == 64,
        "commit evidence contains a request hash");
  check(baseline_commit != nullptr &&
            baseline_commit->evidence.new_active_hash ==
                baseline_commit->evidence.payload_hash,
        "commit evidence promotes the payload hash");
}

void test_state_machine_edges() {
  mi::FakeGpuBackend gpu;
  mi::MaterialHotUpdater updater(&gpu);
  mi::Command reset;
  reset.op = mi::Op::Reset;
  reset.material = "mi_steel";
  reset.version = 1;
  check(updater.execute(reset).status == "accepted", "reset accepted");

  mi::Command submit;
  submit.op = mi::Op::Submit;
  submit.material = "mi_steel";
  submit.key = "edge";
  submit.version = 2;
  submit.params.roughness = 0.2F;
  const mi::Outcome submitted = updater.execute(submit);
  check(submitted.status == "accepted" && submitted.tx == 1,
        "edge submit accepted");

  mi::Command commit;
  commit.op = mi::Op::Commit;
  commit.material = "mi_steel";
  commit.tx = 1;
  check(updater.execute(commit).status == "invalid_transition",
        "commit before validate rejected");

  mi::Command validate;
  validate.op = mi::Op::Validate;
  validate.material = "mi_steel";
  validate.tx = 1;
  check(updater.execute(validate).status == "accepted", "validate accepted");

  mi::Command begin;
  begin.op = mi::Op::UploadBegin;
  begin.material = "mi_steel";
  begin.tx = 1;
  check(updater.execute(begin).status == "accepted", "upload begins");
  check(updater.execute(commit).status == "invalid_transition",
        "commit before upload completion rejected");

  mi::Command cancel;
  cancel.op = mi::Op::Cancel;
  cancel.material = "mi_steel";
  cancel.tx = 1;
  check(updater.execute(cancel).status == "canceled", "mid-flight cancel");
  check(updater.execute(cancel).status == "already_canceled",
        "repeated cancel is no-op");
  check(updater.snapshot().active_version == 1,
        "cancel preserved active material");
}

void test_report_sections() {
  mi::FakeGpuBackend gpu;
  const auto scenarios = mi::build_scenarios();
  const mi::ExperimentResult result = mi::execute_experiment(&gpu, scenarios);
  mi::ReportContext context;
  context.backend = gpu.info();
  context.output_dir = "artifacts/test";
  const std::string report = mi::render_report(result, context);
  check(report.find("## 自动结论") != std::string::npos,
        "report has automatic conclusions");
  check(report.find("## 人工确认") != std::string::npos,
        "report has manual confirmation");
  check(report.find("## 失败后的可执行操作") != std::string::npos,
        "report has executable follow-up");
  check(report.find("**PASS**") != std::string::npos,
        "report states auto pass");

  context.failure = "simulated Vulkan failure";
  const std::string failure = mi::render_failure_report(context);
  check(failure.find("**FAIL**") != std::string::npos,
        "failure report states fail");
  check(failure.find("cmake --build build") != std::string::npos,
        "failure report includes build command");
  const std::string manual = mi::render_manual_confirmation(result, context);
  check(manual.find("待确认") != std::string::npos,
        "manual template is explicitly pending");
}

}  // namespace

int main() {
  test_sha256();
  test_command_roundtrip();
  test_experiment();
  test_state_machine_edges();
  test_report_sections();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "all tests passed\n";
  return 0;
}
