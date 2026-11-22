#pragma once

#include <string>
#include <vector>

namespace lshc {

struct PlatformInfo {
    bool glfwInitialized = false;
    bool glfwVulkanSupported = false;
    std::string glfwVersion;
    std::string error;
    std::vector<std::string> requiredInstanceExtensions;

    bool available() const { return glfwInitialized; }
};

PlatformInfo initializePlatform();
void shutdownPlatform();

}  // namespace lshc

