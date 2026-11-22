#pragma once
#include "codecs.h"
#include <string>
#include <vector>

namespace tcpv {

struct RunOptions {
  std::string texture = "gradient";
  uint32_t width = 128;
  uint32_t height = 128;
  std::vector<CodecId> codecs;        // empty = all codecs
  int cancelAfterDraws = -1;          // >=0: cancel mid-run after N draws
  bool windowed = false;              // ESC/close also cancels
  double psnrThreshold = 30.0;
  std::string runsDir = "runs";
  std::string runId;                  // empty = auto-generate
};

// Execute one auditable preview run. Returns process exit code.
int runPreview(const RunOptions& opt);

// Sub-commands.
int cmdConfirm(const std::string& runsDir, const std::string& runId, int itemId,
               bool approve, const std::string& reviewer);
int cmdVerify(const std::string& runsDir, const std::string& runId);
int cmdReport(const std::string& runsDir, const std::string& runId);
int cmdDoctor();

} // namespace tcpv
