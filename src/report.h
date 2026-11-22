#pragma once
#include "evidence.h"
#include <string>
#include <vector>

namespace tcpv {

// One human-review checklist item, persisted in review.json.
struct ReviewItem {
  int id = 0;
  std::string text;
  std::string state;     // pending | approved | rejected
  std::string reviewer;
  std::string at;
};

struct ReviewFile {
  std::string runId;
  std::vector<ReviewItem> items;
};

bool writeReview(const std::string& path, const ReviewFile& r);
bool readReview(const std::string& path, ReviewFile& r);

// Build the default human-confirmation checklist for a run.
ReviewFile defaultChecklist(const RunManifest& m);

// Generate report.md. Automated conclusions and human confirmation are
// separate sections; failures get an actionable next-steps section.
std::string renderReport(const RunManifest& m, const ReviewFile& review,
                         double psnrThreshold);

// Actionable follow-up commands for a failed/cancelled run.
std::vector<std::string> nextSteps(const RunManifest& m, double psnrThreshold);

} // namespace tcpv
