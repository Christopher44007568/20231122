#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace audit {

enum class ScenarioKind {
  Completed,
  CancelledDuringRecording,
  CancelledDuringExecution,
  BoundaryUndecidable,
};

enum class AutoVerdict {
  Occluded,
  NotOccluded,
  Cancelled,
  Undecidable,
};

struct RawQueryInput {
  uint32_t slot = 0;
  std::string label;
  std::string expectedUse;
  bool beginRecorded = false;
  bool endRecorded = false;
  bool submitted = false;
};

struct QueryResultEvidence {
  uint32_t slot = 0;
  int vkResult = 0;
  uint64_t sampleCount = 0;
  uint64_t availability = 0;
  uint32_t resultFlags = 0;
  bool readable = false;
};

struct ProcessingStep {
  int order = 0;
  std::string stage;
  std::string detail;
  std::string evidenceRef;
};

struct ScenarioRecord {
  std::string id;
  ScenarioKind kind = ScenarioKind::Completed;
  std::string title;
  RawQueryInput input;
  std::vector<ProcessingStep> steps;
  QueryResultEvidence result;
  AutoVerdict verdict = AutoVerdict::Undecidable;
  std::string verdictReason;
  std::string manualStatus;
  std::string manualNote;
  std::vector<std::string> followUpActions;
};

struct EnvironmentEvidence {
  std::string glfwVersion;
  std::string instanceApi;
  std::string deviceName;
  uint32_t apiVersion = 0;
  uint32_t driverVersion = 0;
  bool hiddenWindow = false;
  bool surfaceSupported = false;
  std::string windowModeReason;
};

struct AuditBundle {
  std::string runId;
  std::string createdAtUtc;
  EnvironmentEvidence environment;
  std::vector<ScenarioRecord> scenarios;
  std::string inputSha256;
  std::string ledgerSha256;
};

const char* toString(ScenarioKind kind);
const char* toString(AutoVerdict verdict);
std::string sha256Hex(const std::string& bytes);
std::string sha256File(const std::string& path);

}  // namespace audit
