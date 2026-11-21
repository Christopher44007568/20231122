#pragma once

#include <filesystem>

namespace clipfix {

struct GuiOptions {
    std::filesystem::path workspace;
    std::filesystem::path material;
    int autoExitFrames = 0;
    std::filesystem::path screenshot;
    int screenshotAfterFrames = 2;
    std::string view = "corrected";
    bool showClipSurface = true;
    bool showWireframe = false;
};

int runGui(const GuiOptions& options);

}  // namespace clipfix
