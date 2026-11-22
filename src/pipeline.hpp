#pragma once

#include <cstdint>
#include <string>

namespace kfc {

struct RunOptions {
  std::string inputDir;
  std::string outputDir;
  double budgetMb = 32.0;
  std::uint64_t pauseAfterChunks = 0;
  bool openWindow = false;
  bool disableVulkan = false;
  std::string baselinePath;
  std::string degradedPath;
};

struct DemoOptions {
  std::string outputDir;
  double budgetMb = 32.0;
  std::uint64_t pauseAfterChunks = 3;
  bool openWindow = false;
  bool disableVulkan = false;
};

int commandDemo(const DemoOptions& options);
int commandRun(const RunOptions& options);
int commandResume(const std::string& checkpointPath,
                  const std::string& outputDir,
                  std::uint64_t additionalPauseChunks,
                  bool openWindow,
                  const std::string& baselinePath,
                  const std::string& degradedPath);
int commandVerify(const std::string& recordPath, bool strictReplay);
int commandReplay(const std::string& recordPath);
int commandReloadCheck(const std::string& filePath);
int commandInspect(const std::string& filePath);
int commandCompare(const std::string& leftPath, const std::string& rightPath);
int commandManifest(const std::string& outputDir);

}  // namespace kfc
