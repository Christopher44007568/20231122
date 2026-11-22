#pragma once

#include "rtsync/json_util.hpp"
#include "rtsync/types.hpp"

#include <filesystem>
#include <string>

namespace rtsync {

struct AnalyzeRequest {
  std::string input_path;
  std::string output_dir;
  Limits limits;
};

struct AnalyzeOutcome {
  bool ok = false;
  std::string status;
  std::string message;
  std::filesystem::path artifact_dir;
};

AnalyzeOutcome run_analysis(const AnalyzeRequest& request);

struct ReloadOutcome {
  bool ok = false;
  std::string message;
  Json report = Json::object();
};

ReloadOutcome reload_artifact(const std::string& artifact_dir,
                              const std::string& output_dir);

struct VerifyOutcome {
  bool ok = false;
  std::string message;
  Json report = Json::object();
};

VerifyOutcome verify_artifact(const std::string& artifact_dir);

}  // namespace rtsync

