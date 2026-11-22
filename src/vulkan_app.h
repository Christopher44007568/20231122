#pragma once

#include <filesystem>
#include <string>

namespace hdrprov {

struct AppOptions {
    std::filesystem::path root;
    std::string initialRecord;
    std::filesystem::path screenshot;
    int screenshotFrame = 8;
    int autoExitFrames = 0;
};

int runVulkanApp(const AppOptions& options);

}  // namespace hdrprov
