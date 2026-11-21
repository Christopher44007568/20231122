#pragma once

#include "core/workspace.h"

#include <filesystem>
#include <string>
#include <vector>

namespace clipfix {

struct AcceptanceOptions {
    std::filesystem::path reportDirectory;
    bool fillMissingEvidence = true;
    int requiredRunsPerObject = 2;
};

struct AcceptanceReport {
    bool pass = false;
    std::filesystem::path jsonPath;
    std::filesystem::path markdownPath;
    std::filesystem::path timelineCsvPath;
    std::vector<std::string> failures;
    nlohmann::json data;
};

AcceptanceReport runAcceptance(
    Workspace& workspace,
    const AcceptanceOptions& options);

}  // namespace clipfix
