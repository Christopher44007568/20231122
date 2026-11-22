#pragma once

#include "gbi/workspace.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gbi {

struct VerificationIssue {
  std::string path;
  std::string message;
};

struct VerificationReport {
  std::string schema = "gbi.verification/1";
  std::string verified_at;
  bool ok = false;
  std::uint64_t originals_checked = 0;
  std::uint64_t captures_checked = 0;
  std::uint64_t views_checked = 0;
  std::uint64_t attachments_checked = 0;
  std::uint64_t probes_checked = 0;
  std::uint64_t exports_checked = 0;
  std::vector<VerificationIssue> issues;
  std::vector<std::string> warnings;

  std::string toJson() const;
  std::string toText() const;
};

struct CommonOptions {
  fs::path workspace = "gbuffer-workspace";
};

struct CaptureOptions : CommonOptions {
  std::string anchor;
  std::string object_id;
  std::string revision;
  std::string note;
  std::uint32_t width = 640;
  std::uint32_t height = 360;
};

struct CompareOptions : CommonOptions {
  std::string object_id;
  std::string left_anchor = "A0";
  std::string right_anchor = "A1";
  std::string left_capture;
  std::string right_capture;
};

struct ExportOptions : CompareOptions {
  fs::path output;
};

int commandInit(const CommonOptions& options, bool force);
int commandStatus(const CommonOptions& options);
int commandAnchors(const CommonOptions& options);
int commandCapture(const CaptureOptions& options);
int commandCorrect(const CommonOptions& options, const std::string& object_id,
                   const std::string& albedo, const std::string& roughness,
                   const std::string& metallic, bool clear);
int commandCompare(const CompareOptions& options);
int commandExport(const ExportOptions& options);
int commandVerify(const CommonOptions& options);
int commandResume(const CommonOptions& options);
int commandDemo(const CommonOptions& options, bool force);
int commandRun(const CommonOptions& options);

VerificationReport verifyWorkspace(const Workspace& workspace);
fs::path defaultOriginalAsset();

}  // namespace gbi
