#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mi {

struct MaterialParams {
  std::array<float, 4> base_color{0.25F, 0.30F, 0.36F, 1.0F};
  float roughness{0.55F};
  float metallic{0.10F};
  std::uint32_t shader_variant{1};

  bool operator==(const MaterialParams& other) const;
  bool operator!=(const MaterialParams& other) const { return !(*this == other); }
};

enum class Op {
  Reset,
  Submit,
  Validate,
  UploadBegin,
  UploadComplete,
  Commit,
  Cancel,
};

struct Command {
  std::string scenario;
  std::uint64_t scenario_seq{0};
  Op op{Op::Reset};
  std::string material;
  std::string key;
  std::uint64_t tx{0};
  std::uint64_t version{0};
  MaterialParams params{};
  std::string expected;
};

enum class TxState {
  Submitted,
  Validated,
  Uploading,
  Ready,
  Committed,
  Canceled,
};

struct MaterialState {
  std::string id{"mi_steel"};
  std::uint64_t active_version{1};
  MaterialParams active_params{};
  std::string active_payload_hash;
  std::uint64_t commit_count{0};
};

struct Transaction {
  std::uint64_t id{0};
  Command request;
  TxState state{TxState::Submitted};
  std::string request_hash;
  std::string payload_hash;
  std::string gpu_payload_id;
  bool gpu_resident{false};
};

struct Outcome {
  Outcome() = default;
  Outcome(std::string status_value, std::string code_value,
          std::string detail_value, std::uint64_t tx_value = 0,
          bool active_changed_value = false, bool duplicate_value = false,
          bool readback_verified_value = false,
          bool gpu_released_value = false,
          std::uint64_t evicted_tx_value = 0)
      : status(std::move(status_value)),
        code(std::move(code_value)),
        detail(std::move(detail_value)),
        tx(tx_value),
        active_changed(active_changed_value),
        duplicate(duplicate_value),
        gpu_readback_verified(readback_verified_value),
        gpu_released(gpu_released_value),
        evicted_tx(evicted_tx_value) {}

  std::string status;
  std::string code;
  std::string detail;
  std::uint64_t tx{0};
  bool active_changed{false};
  bool duplicate{false};
  bool gpu_readback_verified{false};
  bool gpu_released{false};
  std::uint64_t evicted_tx{0};
  std::string prior_active_hash;
  std::string new_active_hash;
  std::string request_hash;
  std::string payload_hash;
  std::string gpu_payload_id;
};

struct StateSnapshot {
  std::string material;
  std::uint64_t active_version{0};
  std::string active_payload_hash;
  std::uint64_t pending_tx{0};
  std::string pending_stage;
  bool pending_gpu_resident{false};
  std::uint64_t transaction_count{0};
  std::uint64_t transaction_history_count{0};
  std::uint64_t commit_count{0};
  std::uint64_t cancel_count{0};
  std::uint64_t duplicate_count{0};
  std::uint64_t rejected_count{0};
};

struct Evidence {
  std::string request_hash;
  std::string payload_hash;
  std::string gpu_payload_id;
  bool gpu_readback_verified{false};
  bool gpu_released{false};
  std::string prior_active_hash;
  std::string new_active_hash;
};

struct Check {
  std::string id;
  bool ok{false};
  std::string detail;
};

struct StepRecord {
  std::uint64_t global_seq{0};
  Command command;
  StateSnapshot before;
  StateSnapshot after;
  Outcome outcome;
  Evidence evidence;
  std::string prev_hash;
  std::string core_json;
  std::string event_hash;
};

struct ScenarioResult {
  std::string name;
  std::string purpose;
  std::string control_of;
  std::vector<StepRecord> steps;
  std::vector<Check> checks;
  bool passed{false};
};

struct ReplayResult {
  bool ok{false};
  std::size_t original_steps{0};
  std::size_t replayed_steps{0};
  std::string first_mismatch;
  std::string original_final_hash;
  std::string replayed_final_hash;
};

const char* to_string(Op op);
const char* to_string(TxState state);
std::string canonical_params(const MaterialParams& params, std::uint64_t version,
                             const std::string& material);

}  // namespace mi
