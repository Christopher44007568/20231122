#pragma once

#include "model.hpp"
#include "vulkan_renderer.hpp"

#include <filesystem>
#include <string>

namespace rr {

struct RunOptions {
    std::filesystem::path inputPath;
    std::filesystem::path outputDirectory;
    Limits limits;
    RenderOptions render;
    bool previewWindow = false;
};

struct RunSummary {
    int exitCode = 1;
    std::string status;
    std::string outputDirectory;
    std::uint64_t referenceFailures = 0;
};

RunSummary runRegression(const RunOptions& options);

}  // namespace rr
