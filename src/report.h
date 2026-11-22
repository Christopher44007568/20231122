#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class Severity { Info, Warning, Error };

struct Finding {
  Severity severity = Severity::Info;
  std::string category;  // e.g. "extension", "image-diff", "resource-state"
  std::string message;
};

struct EvidenceFile {
  std::string path;
  std::string role;
  uint64_t fnv1a = 0;
};

// Diagnostic report. Automated conclusions and manual confirmation items are
// kept in separate collections and rendered into separate report sections.
struct Report {
  std::string title;
  std::vector<std::string> contextLines;
  std::vector<Finding> autoFindings;      // machine-generated conclusions
  std::vector<std::string> manualChecks;  // human review checklist
  std::vector<std::string> nextActions;   // actionable follow-ups
  std::vector<EvidenceFile> evidence;

  bool hasErrors() const;
  bool hasWarnings() const;
};

// Writes report.md and report.json into dir (must exist).
// Throws std::runtime_error on I/O failure.
void writeReport(const Report& report, const std::string& dir);
