#include "platform.hpp"

#include <GLFW/glfw3.h>

namespace lshc {
namespace {

std::string g_lastGlfwError;

void glfwErrorCallback(int code, const char* description)
{
    if (!g_lastGlfwError.empty()) {
        return;
    }
    g_lastGlfwError = "GLFW error " + std::to_string(code) + ": " +
                      (description != nullptr ? description : "unknown");
}

bool initialized = false;

}  // namespace

PlatformInfo initializePlatform()
{
    g_lastGlfwError.clear();
    glfwSetErrorCallback(glfwErrorCallback);
    PlatformInfo info;
    if (glfwInit() == GLFW_FALSE) {
        info.error = g_lastGlfwError.empty()
                         ? "glfwInit returned false"
                         : g_lastGlfwError;
        info.glfwVersion = "unavailable (headless fallback)";
        return info;
    }
    initialized = true;
    info.glfwInitialized = true;
    const char* version = glfwGetVersionString();
    info.glfwVersion = version != nullptr ? version : "unknown";
    info.glfwVulkanSupported = glfwVulkanSupported() == GLFW_TRUE;
    if (info.glfwVulkanSupported) {
        uint32_t count = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&count);
        if (extensions != nullptr) {
            for (uint32_t index = 0; index < count; ++index) {
                if (extensions[index] != nullptr) {
                    info.requiredInstanceExtensions.emplace_back(
                        extensions[index]);
                }
            }
        }
    }
    return info;
}

void shutdownPlatform()
{
    if (initialized) {
        glfwTerminate();
        initialized = false;
    }
}

}  // namespace lshc
