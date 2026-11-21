#pragma once

#include <cstdint>
#include <string>

#include "mi/types.hpp"

namespace mi {

struct BackendInfo {
  std::string name;
  std::string device;
  std::string driver;
  std::string api_version;
  bool asynchronous_submit{false};
};

class GpuBackend {
 public:
  virtual ~GpuBackend() = default;

  virtual BackendInfo info() const = 0;
  virtual void reset() = 0;
  virtual bool begin_upload(const Transaction& tx, std::string* detail) = 0;
  virtual bool complete_upload(const Transaction& tx, std::string* detail) = 0;
  virtual bool commit_upload(const Transaction& tx, std::string* detail) = 0;
  virtual bool cancel_upload(const Transaction& tx, std::string* detail) = 0;
  virtual void poll() = 0;
};

class FakeGpuBackend final : public GpuBackend {
 public:
  BackendInfo info() const override;
  void reset() override;
  bool begin_upload(const Transaction& tx, std::string* detail) override;
  bool complete_upload(const Transaction& tx, std::string* detail) override;
  bool commit_upload(const Transaction& tx, std::string* detail) override;
  bool cancel_upload(const Transaction& tx, std::string* detail) override;
  void poll() override;

 private:
  std::uint64_t generation_{0};
};

}  // namespace mi
