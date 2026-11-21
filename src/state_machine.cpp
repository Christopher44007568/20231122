#include "mi/state_machine.hpp"

#include <iomanip>
#include <sstream>

#include "mi/json.hpp"
#include "mi/sha256.hpp"

namespace mi {

std::string canonical_params(const MaterialParams& params, std::uint64_t version,
                             const std::string& material) {
  std::ostringstream out;
  out << "material=" << material << ";version=" << version
      << ";color=" << json_number(params.base_color[0]) << ","
      << json_number(params.base_color[1]) << ","
      << json_number(params.base_color[2]) << ","
      << json_number(params.base_color[3])
      << ";roughness=" << json_number(params.roughness)
      << ";metallic=" << json_number(params.metallic)
      << ";variant=" << params.shader_variant;
  return out.str();
}

const char* to_string(Op op) {
  switch (op) {
    case Op::Reset: return "reset";
    case Op::Submit: return "submit";
    case Op::Validate: return "validate";
    case Op::UploadBegin: return "upload_begin";
    case Op::UploadComplete: return "upload_complete";
    case Op::Commit: return "commit";
    case Op::Cancel: return "cancel";
  }
  return "unknown";
}

const char* to_string(TxState state) {
  switch (state) {
    case TxState::Submitted: return "submitted";
    case TxState::Validated: return "validated";
    case TxState::Uploading: return "uploading";
    case TxState::Ready: return "ready";
    case TxState::Committed: return "committed";
    case TxState::Canceled: return "canceled";
  }
  return "unknown";
}

namespace {

bool is_rejection(const Outcome& outcome) {
  return outcome.status == "rejected" ||
         outcome.status == "invalid_transition" ||
         outcome.status == "idempotency_conflict" ||
         outcome.status == "stale_version" ||
         outcome.status == "busy" ||
         outcome.status == "too_late";
}

}  // namespace

BackendInfo FakeGpuBackend::info() const {
  return BackendInfo{"fake", "deterministic-null", "built-in", "n/a", false};
}

void FakeGpuBackend::reset() { generation_ = 0; }

bool FakeGpuBackend::begin_upload(const Transaction&, std::string* detail) {
  ++generation_;
  if (detail != nullptr) {
    *detail = "fake upload queued, generation " + std::to_string(generation_);
  }
  return true;
}

bool FakeGpuBackend::complete_upload(const Transaction&, std::string* detail) {
  if (detail != nullptr) {
    *detail = "fake readback verified";
  }
  return true;
}

bool FakeGpuBackend::commit_upload(const Transaction&, std::string* detail) {
  if (detail != nullptr) {
    *detail = "fake staged resource promoted";
  }
  return true;
}

bool FakeGpuBackend::cancel_upload(const Transaction&, std::string* detail) {
  if (detail != nullptr) {
    *detail = "fake staged resource released";
  }
  return true;
}

void FakeGpuBackend::poll() {}

MaterialHotUpdater::MaterialHotUpdater(GpuBackend* gpu) : gpu_(gpu) {
  reset();
}

void MaterialHotUpdater::reset() {
  material_ = MaterialState{};
  material_.id = "mi_steel";
  material_.active_version = 1;
  material_.active_params = MaterialParams{};
  material_.active_payload_hash =
      sha256_hex(canonical_params(material_.active_params, 1, material_.id));
  material_.commit_count = 0;
  pending_.clear();
  history_.clear();
  idempotency_.clear();
  idempotency_payload_.clear();
  next_tx_ = 1;
  cancel_count_ = 0;
  duplicate_count_ = 0;
  rejected_count_ = 0;
  gpu_->reset();
}

Outcome MaterialHotUpdater::execute(const Command& command) {
  gpu_->poll();
  switch (command.op) {
    case Op::Reset: return on_reset(command);
    case Op::Submit: return on_submit(command);
    case Op::Validate: return on_validate(command);
    case Op::UploadBegin: return on_upload_begin(command);
    case Op::UploadComplete: return on_upload_complete(command);
    case Op::Commit: return on_commit(command);
    case Op::Cancel: return on_cancel(command);
  }
  return Outcome{"rejected", "unknown_op", "unrecognised operation", command.tx};
}

Outcome MaterialHotUpdater::on_reset(const Command& command) {
  material_ = MaterialState{};
  material_.id = command.material.empty() ? "mi_steel" : command.material;
  material_.active_version = command.version == 0 ? 1 : command.version;
  material_.active_params = command.params;
  material_.active_payload_hash = sha256_hex(
      canonical_params(material_.active_params, material_.active_version, material_.id));
  material_.commit_count = 0;
  pending_.clear();
  history_.clear();
  idempotency_.clear();
  idempotency_payload_.clear();
  next_tx_ = 1;
  cancel_count_ = 0;
  duplicate_count_ = 0;
  rejected_count_ = 0;
  gpu_->reset();
  return Outcome{"accepted", "reset", "fresh material instance at baseline",
                 0, false, false,
                 false, false, 0};
}

Outcome MaterialHotUpdater::on_submit(const Command& command) {
  if (command.material.empty() || command.key.empty()) {
    ++rejected_count_;
    return Outcome{"rejected", "invalid_input",
                   "material and idempotency key are required", 0};
  }
  if (command.material != material_.id) {
    ++rejected_count_;
    return Outcome{"rejected", "unknown_material",
                   "only the experiment material is accepted", 0};
  }

  const std::string idem_key = command.material + "\n" + command.key;
  const std::string req_hash = request_hash(command);
  const auto existing = idempotency_.find(idem_key);
  if (existing != idempotency_.end()) {
    if (idempotency_payload_[idem_key] == req_hash) {
      ++duplicate_count_;
      Outcome outcome{"duplicate_accepted", "deduplicated",
                      "same key and payload returned the original transaction",
                      existing->second, false, true,
                      false, false, 0};
      outcome.request_hash = req_hash;
      if (const Transaction* original = find_tx(existing->second);
          original != nullptr) {
        outcome.payload_hash = original->payload_hash;
        outcome.gpu_payload_id = original->gpu_payload_id;
      }
      return outcome;
    }
    ++rejected_count_;
    return Outcome{"idempotency_conflict", "payload_mismatch",
                   "key already belongs to a different payload",
                   existing->second};
  }
  if (find_pending() != nullptr) {
    ++rejected_count_;
    return Outcome{"busy", "tx_in_flight",
                   "another transaction is already pending"};
  }
  if (command.version <= material_.active_version) {
    ++rejected_count_;
    return Outcome{"stale_version", "version_not_newer",
                   "target version must be newer than the active version", 0};
  }

  Transaction tx;
  tx.id = next_tx_++;
  tx.request = command;
  tx.state = TxState::Submitted;
  tx.request_hash = req_hash;
  tx.payload_hash = payload_hash(command);
  pending_[tx.id] = tx;
  history_.push_back(tx);
  idempotency_[idem_key] = tx.id;
  idempotency_payload_[idem_key] = req_hash;

  Outcome outcome{"accepted", "submitted",
                  "transaction created after idempotency and version checks",
                  tx.id};
  outcome.request_hash = tx.request_hash;
  outcome.payload_hash = tx.payload_hash;
  return outcome;
}

Outcome MaterialHotUpdater::on_validate(const Command& command) {
  Transaction* tx = find_tx(command.tx);
  if (tx == nullptr || tx->state != TxState::Submitted ||
      pending_.count(command.tx) == 0) {
    ++rejected_count_;
    return Outcome{"invalid_transition", "validate_requires_submitted",
                   "validate is only legal from the submitted stage", command.tx};
  }
  tx->state = TxState::Validated;
  sync_history(*tx);
  return Outcome{"accepted", "validated",
                 "request payload and target version validated", tx->id};
}

Outcome MaterialHotUpdater::on_upload_begin(const Command& command) {
  Transaction* tx = find_tx(command.tx);
  if (tx == nullptr || tx->state != TxState::Validated ||
      pending_.count(command.tx) == 0) {
    ++rejected_count_;
    return Outcome{"invalid_transition", "upload_begin_requires_validated",
                   "GPU upload cannot begin before validation", command.tx};
  }

  std::string detail;
  if (!gpu_->begin_upload(*tx, &detail)) {
    ++rejected_count_;
    return Outcome{"rejected", "gpu_upload_failed", detail, tx->id};
  }
  tx->state = TxState::Uploading;
  tx->gpu_resident = true;
  tx->gpu_payload_id = "gpu-tx-" + std::to_string(tx->id);
  sync_history(*tx);

  Outcome outcome{"accepted", "uploading",
                  "asynchronous GPU upload submitted; commit is not allowed yet",
                  tx->id};
  outcome.gpu_readback_verified = false;
  outcome.request_hash = tx->request_hash;
  outcome.payload_hash = tx->payload_hash;
  outcome.gpu_payload_id = tx->gpu_payload_id;
  return outcome;
}

Outcome MaterialHotUpdater::on_upload_complete(const Command& command) {
  Transaction* tx = find_tx(command.tx);
  if (tx == nullptr || tx->state != TxState::Uploading ||
      pending_.count(command.tx) == 0) {
    ++rejected_count_;
    return Outcome{"invalid_transition", "upload_complete_requires_uploading",
                   "upload completion is out of order", command.tx};
  }

  std::string detail;
  if (!gpu_->complete_upload(*tx, &detail)) {
    ++rejected_count_;
    return Outcome{"rejected", "gpu_readback_failed", detail, tx->id};
  }
  tx->state = TxState::Ready;
  sync_history(*tx);
  Outcome outcome{"accepted", "ready_to_commit",
                  "GPU readback matched the immutable payload", tx->id};
  outcome.gpu_readback_verified = true;
  outcome.request_hash = tx->request_hash;
  outcome.payload_hash = tx->payload_hash;
  outcome.gpu_payload_id = tx->gpu_payload_id;
  return outcome;
}

Outcome MaterialHotUpdater::on_commit(const Command& command) {
  Transaction* tx = find_tx(command.tx);
  if (tx == nullptr || tx->state != TxState::Ready ||
      pending_.count(command.tx) == 0) {
    ++rejected_count_;
    return Outcome{"invalid_transition", "commit_requires_ready",
                   "commit requires a verified ready transaction", command.tx};
  }

  std::string detail;
  if (!gpu_->commit_upload(*tx, &detail)) {
    ++rejected_count_;
    return Outcome{"rejected", "gpu_commit_failed", detail, tx->id};
  }

  const std::string prior_hash = material_.active_payload_hash;
  tx->state = TxState::Committed;
  material_.active_version = tx->request.version;
  material_.active_params = tx->request.params;
  material_.active_payload_hash = tx->payload_hash;
  ++material_.commit_count;

  const std::uint64_t committed_id = tx->id;
  const std::string request_hash_value = tx->request_hash;
  const std::string payload_hash_value = tx->payload_hash;
  const std::string gpu_payload_id_value = tx->gpu_payload_id;
  sync_history(*tx);
  pending_.erase(committed_id);
  Outcome outcome{"committed", "active_swapped",
                  "GPU resource promoted and active material switched", committed_id};
  outcome.active_changed = true;
  outcome.gpu_readback_verified = true;
  outcome.prior_active_hash = prior_hash;
  outcome.new_active_hash = material_.active_payload_hash;
  outcome.request_hash = request_hash_value;
  outcome.payload_hash = payload_hash_value;
  outcome.gpu_payload_id = gpu_payload_id_value;
  return outcome;
}

Outcome MaterialHotUpdater::on_cancel(const Command& command) {
  Transaction* tx = find_tx(command.tx);
  if (tx == nullptr) {
    ++rejected_count_;
    return Outcome{"rejected", "unknown_tx", "transaction does not exist", command.tx};
  }
  if (tx->state == TxState::Committed) {
    ++rejected_count_;
    return Outcome{"too_late", "already_committed",
                   "cancellation cannot undo a committed hot update", tx->id};
  }
  if (tx->state == TxState::Canceled) {
    return Outcome{"already_canceled", "no_effect",
                   "repeated cancellation is a no-op", tx->id};
  }

  bool released = false;
  if (tx->gpu_resident) {
    std::string detail;
    if (!gpu_->cancel_upload(*tx, &detail)) {
      ++rejected_count_;
      return Outcome{"rejected", "gpu_cancel_failed", detail, tx->id};
    }
    released = true;
  }

  const std::string prior_hash = material_.active_payload_hash;
  tx->state = TxState::Canceled;
  tx->gpu_resident = false;
  const std::uint64_t canceled_id = tx->id;
  const std::string request_hash_value = tx->request_hash;
  const std::string payload_hash_value = tx->payload_hash;
  const std::string gpu_payload_id_value = tx->gpu_payload_id;
  sync_history(*tx);
  pending_.erase(canceled_id);
  ++cancel_count_;

  Outcome outcome{"canceled", "rolled_back",
                  "transaction invalidated; active material was never changed",
                  canceled_id};
  outcome.active_changed = false;
  outcome.gpu_released = released;
  outcome.prior_active_hash = prior_hash;
  outcome.new_active_hash = material_.active_payload_hash;
  outcome.request_hash = request_hash_value;
  outcome.payload_hash = payload_hash_value;
  outcome.gpu_payload_id = gpu_payload_id_value;
  return outcome;
}

Transaction* MaterialHotUpdater::find_tx(std::uint64_t id) {
  const auto pending_it = pending_.find(id);
  if (pending_it != pending_.end()) {
    return &pending_it->second;
  }
  for (auto& tx : history_) {
    if (tx.id == id) {
      return &tx;
    }
  }
  return nullptr;
}

const Transaction* MaterialHotUpdater::find_pending() const {
  return pending_.empty() ? nullptr : &pending_.begin()->second;
}

Transaction* MaterialHotUpdater::find_pending() {
  return pending_.empty() ? nullptr : &pending_.begin()->second;
}

std::string MaterialHotUpdater::request_hash(const Command& command) const {
  return sha256_hex(canonical_params(command.params, command.version,
                                    command.material) +
                    ";key=" + command.key);
}

std::string MaterialHotUpdater::payload_hash(const Command& command) const {
  return sha256_hex(canonical_params(command.params, command.version,
                                    command.material));
}

void MaterialHotUpdater::sync_history(const Transaction& tx) {
  for (auto& historical : history_) {
    if (historical.id == tx.id) {
      historical = tx;
      return;
    }
  }
}

StateSnapshot MaterialHotUpdater::snapshot() const {
  StateSnapshot snapshot;
  snapshot.material = material_.id;
  snapshot.active_version = material_.active_version;
  snapshot.active_payload_hash = material_.active_payload_hash;
  snapshot.transaction_count = pending_.size();
  snapshot.transaction_history_count = history_.size();
  snapshot.commit_count = material_.commit_count;
  snapshot.cancel_count = cancel_count_;
  snapshot.duplicate_count = duplicate_count_;
  snapshot.rejected_count = rejected_count_;
  const auto* pending = find_pending();
  if (pending != nullptr) {
    snapshot.pending_tx = pending->id;
    snapshot.pending_stage = to_string(pending->state);
    snapshot.pending_gpu_resident = pending->gpu_resident;
  }
  return snapshot;
}

}  // namespace mi
