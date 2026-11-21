#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mi/backend.hpp"
#include "mi/types.hpp"

namespace mi {

class MaterialHotUpdater {
 public:
  explicit MaterialHotUpdater(GpuBackend* gpu);

  void reset();
  Outcome execute(const Command& command);

  StateSnapshot snapshot() const;
  const MaterialState& material() const { return material_; }
  const std::vector<Transaction>& history() const { return history_; }

 private:
  Outcome on_reset(const Command& command);
  Outcome on_submit(const Command& command);
  Outcome on_validate(const Command& command);
  Outcome on_upload_begin(const Command& command);
  Outcome on_upload_complete(const Command& command);
  Outcome on_commit(const Command& command);
  Outcome on_cancel(const Command& command);

  Transaction* find_tx(std::uint64_t id);
  const Transaction* find_pending() const;
  Transaction* find_pending();
  std::string request_hash(const Command& command) const;
  std::string payload_hash(const Command& command) const;
  void sync_history(const Transaction& tx);

  GpuBackend* gpu_;
  MaterialState material_;
  std::map<std::uint64_t, Transaction> pending_;
  std::vector<Transaction> history_;
  std::map<std::string, std::uint64_t> idempotency_;
  std::map<std::string, std::string> idempotency_payload_;
  std::uint64_t next_tx_{1};
  std::uint64_t cancel_count_{0};
  std::uint64_t duplicate_count_{0};
  std::uint64_t rejected_count_{0};
};

}  // namespace mi
