#pragma once

#include "format.hpp"
#include "image.hpp"
#include "renderer.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace msphere {

struct PhaseEvent {
  std::string name;
  std::string timestamp_utc;
  uint64_t elapsed_us = 0;
  std::string detail;
};

struct AcceptanceCheck {
  std::string id;
  bool passed = false;
  std::string expected;
  std::string actual;
};

struct ArtifactEvidence {
  std::string role;
  std::string path;
  uint64_t size = 0;
  std::string sha256;
  std::string layout;
};

struct InvalidInputEvidence {
  std::string case_name;
  std::string input_path;
  std::string checked_at_utc;
  uint64_t elapsed_us = 0;
  uint64_t file_size = 0;
  uint64_t max_bytes = 0;
  std::string error_code;
  std::string field;
  std::string detail;
  uint64_t raw_offset = 0;
  uint64_t observed = 0;
  uint64_t limit = 0;
};

struct EnergySample {
  uint32_t instance_index = 0;
  float expected_energy = 0.0F;
  uint32_t x0 = 0;
  uint32_t y0 = 0;
  uint32_t x1 = 0;
  uint32_t y1 = 0;
  double baseline_mean_luma = 0.0;
  double anomaly_mean_luma = 0.0;
  double relative_change = 0.0;
};

struct EvidenceReport {
  std::string title;
  std::string run_started_utc;
  std::string run_finished_utc;
  uint64_t run_elapsed_us = 0;
  std::string source_path;
  std::string preserved_path;
  std::string source_sha256_before;
  std::string source_sha256_after;
  bool source_unchanged = false;
  const ParsedInput* input = nullptr;
  const RenderResult* render = nullptr;
  DiffStats diff;
  std::vector<PhaseEvent> phases;
  std::vector<AcceptanceCheck> checks;
  std::vector<ArtifactEvidence> artifacts;
  std::vector<InvalidInputEvidence> invalid_inputs;
  std::vector<EnergySample> energy_samples;
};

std::string utc_now_iso8601();
uint64_t monotonic_microseconds();

bool write_evidence_report(const std::filesystem::path& path,
                           const EvidenceReport& report, std::string* error);
bool write_invalid_input_report(const std::filesystem::path& path,
                                const InvalidInputEvidence& evidence,
                                std::string* error);
bool write_acceptance_markdown(const std::filesystem::path& path,
                               const EvidenceReport& report,
                               std::string* error);

}  // namespace msphere
