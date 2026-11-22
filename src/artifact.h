#pragma once

#include "pipeline.h"

#include <string>

namespace ph {

class Json;

bool writeRunArtifact(const std::string& path, const RunResult& result, std::string& error);
bool writeCheckpointArtifact(const std::string& path, const RunResult& result,
                             std::string& error);
bool writeBlockedArtifact(const std::string& path, const AppConfig& config,
                          const ResourcePlan& plan, std::string& error);

bool loadCheckpointArtifact(const std::string& path, LoadedCheckpoint& loaded,
                            std::string& error);
bool verifyArtifact(const std::string& path, std::string& report_json, std::string& error);

std::string summarizeRunJson(const RunResult& result);
std::string summarizeCheckpointJson(const RunResult& result);

}  // namespace ph
