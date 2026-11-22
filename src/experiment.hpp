#pragma once

#include <cstdint>
#include <filesystem>

namespace offscreen {

struct ExperimentOptions {
    std::filesystem::path recordPath;
    std::filesystem::path outputDirectory;
    std::uint32_t width = 320;
    std::uint32_t height = 200;
    bool doubleCapture = true;
};

int runExperiment(const ExperimentOptions& options);

}  // namespace offscreen
