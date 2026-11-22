#pragma once

#include <string>
#include <vector>

struct GLFWwindow;

namespace rr {

struct PlatformInfo {
    bool initialized = false;
    bool vulkanSupported = false;
    int versionMajor = 0;
    int versionMinor = 0;
    int versionRevision = 0;
    std::string versionString;
    std::string error;
    std::vector<std::string> requiredVulkanExtensions;
};

PlatformInfo initializePlatform();
void shutdownPlatform();

struct PreviewWindow {
    GLFWwindow* handle = nullptr;
    bool open = false;
    std::string error;
};

PreviewWindow openPreviewWindow(const std::string& title, int width, int height);
void pollPreviewWindow(const PreviewWindow& window);
bool previewWindowShouldClose(const PreviewWindow& window);
void closePreviewWindow(PreviewWindow& window);

}  // namespace rr
