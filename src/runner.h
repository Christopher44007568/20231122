#pragma once

#include "core/material.h"

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace ssor {

struct RunOptions {
    std::string materialPath = "materials/raw/outline_repeat.ssoraw";
    std::string outDir;
    bool verbose = false;
};

struct RunResult {
    int exitCode = 1;
    bool ok = false;
    std::string materialPath;
    std::string outDir;
    std::string error;
    std::shared_ptr<RawMaterial> material;
    nlohmann::ordered_json report;
};

RunResult runMaterial(const RunOptions& options);

bool writeSha256Sums(const std::string& directory, std::string* error);
bool writeJsonFile(const std::string& path, const nlohmann::ordered_json& json);

}  // namespace ssor
