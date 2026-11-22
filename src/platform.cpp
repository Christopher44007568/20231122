#include "platform.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstddef>
#include <cstdint>

namespace rr {
namespace {

std::string g_lastGlfwError;

void glfwErrorCallback(int code, const char* description) {
    g_lastGlfwError = "GLFW error " + std::to_string(code) + ": " +
                      (description ? description : "unknown");
}

}  // namespace

PlatformInfo initializePlatform() {
    PlatformInfo info;
    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() != GLFW_TRUE) {
        info.error = g_lastGlfwError.empty() ? "glfwInit failed" : g_lastGlfwError;
        return info;
    }
    info.initialized = true;
    glfwGetVersion(&info.versionMajor, &info.versionMinor, &info.versionRevision);
    const char* version = glfwGetVersionString();
    info.versionString = version ? version : "unknown";
    info.vulkanSupported = glfwVulkanSupported() == GLFW_TRUE;
    if (info.vulkanSupported) {
        std::uint32_t count = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&count);
        for (std::uint32_t i = 0; i < count; ++i) {
            info.requiredVulkanExtensions.emplace_back(extensions[i]);
        }
    } else {
        info.error = "GLFW reports that Vulkan is unavailable; offscreen rendering will still be attempted";
    }
    return info;
}

void shutdownPlatform() {
    glfwTerminate();
}

PreviewWindow openPreviewWindow(const std::string& title, int width, int height) {
    PreviewWindow window;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    window.handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window.handle) {
        window.error = g_lastGlfwError.empty() ? "glfwCreateWindow failed" : g_lastGlfwError;
        return window;
    }
    window.open = true;
    return window;
}

void pollPreviewWindow(const PreviewWindow& window) {
    if (window.open) {
        glfwPollEvents();
    }
}

bool previewWindowShouldClose(const PreviewWindow& window) {
    return !window.open || !window.handle ||
           glfwWindowShouldClose(window.handle) == GLFW_TRUE;
}

void closePreviewWindow(PreviewWindow& window) {
    if (window.handle) {
        glfwDestroyWindow(window.handle);
        window.handle = nullptr;
    }
    window.open = false;
}

}  // namespace rr
