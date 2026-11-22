#pragma once
#include "image.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tcpv {

// Append-only JSONL event log: the auditable backbone of a run.
// Every draw call, resource-state transition, framebuffer capture and
// metric is one line, correlated by ids.
class EventLog {
public:
  bool open(const std::string& path);
  void close();

  void lifecycle(const std::string& phase, const std::string& detail);
  void resourceState(const std::string& resourceId, const std::string& kind,
                     const std::string& from, const std::string& to,
                     const std::string& drawCallId);
  void framebuffer(const std::string& framebufferId, const std::string& format,
                   uint32_t w, uint32_t h, const std::string& drawCallId);
  void drawCall(const std::string& drawCallId, const std::string& resourceId,
                const std::string& framebufferId, uint32_t vertexCount,
                const std::string& pipeline);
  void capture(const std::string& drawCallId, const std::string& pngPath,
               const std::string& sha256);
  void metric(const std::string& drawCallId, const std::string& name,
              double value);
  void cancellation(const std::string& atStep, const std::string& reason);
  void error(const std::string& where, const std::string& message);

private:
  void emit(const std::string& line);
  std::string path_;
  uint64_t seq_ = 0;
};

struct VariantMetrics {
  std::string codec;
  std::string generation; // legacy | new
  std::string drawCallId;
  double psnr = 0.0;
  double meanAbsError = 0.0;
  uint32_t maxChannelError = 0;
  bool pass = false;
  std::string refPng, decPng, diffPng;
};

struct RunManifest {
  int schema = 2;                 // v2 current; v1 legacy still readable
  std::string runId;
  std::string startedAt;
  std::string finishedAt;
  std::string status;             // COMPLETED | CANCELLED | FAILED
  std::string device;
  std::string texture;
  uint32_t width = 0, height = 0;
  std::string cancelReason;
  std::vector<std::string> completedSteps;
  std::vector<std::string> skippedSteps;
  std::vector<VariantMetrics> variants;
  std::map<std::string, std::string> artifacts; // path -> sha256
};

bool writeManifest(const std::string& path, const RunManifest& m);
// Reads both schema v1 (legacy) and v2 manifests. v1 uses flat field names
// and lacks step/artifact sections; missing parts are defaulted.
bool readManifest(const std::string& path, RunManifest& m, std::string& err);

// Pixel comparison helpers (CPU, on captured framebuffers).
double computePsnr(const Image& a, const Image& b);
double computeMeanAbsError(const Image& a, const Image& b);
uint32_t computeMaxChannelError(const Image& a, const Image& b);
Image makeDiffHeatmap(const Image& a, const Image& b);

} // namespace tcpv
