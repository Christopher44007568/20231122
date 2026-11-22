#include "glfw_session.hpp"

#include <GLFW/glfw3.h>

#include <cstring>

namespace vegd {
namespace {

std::string lastGlfwError;

void glfwErrorCallback(int code, const char* description) {
    lastGlfwError = "GLFW error " + std::to_string(code) + ": " +
                    (description != nullptr ? description : "unknown");
}

}  // namespace

GlfwSession::~GlfwSession() {
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (initialized_) {
        glfwTerminate();
        initialized_ = false;
    }
}

bool GlfwSession::initialize(bool interactive, std::string& error) {
    interactive_ = interactive;
    lastGlfwError.clear();
    glfwSetErrorCallback(glfwErrorCallback);
    if (!interactive) {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    }
    if (glfwInit() != GLFW_TRUE) {
        error = lastGlfwError.empty() ? "glfwInit failed" : lastGlfwError;
        return false;
    }
    initialized_ = true;
    const char* version = glfwGetVersionString();
    version_ = version != nullptr ? version : "unknown";
    return true;
}

bool GlfwSession::createWindow(std::string& error) {
    if (!initialized_) {
        error = "GLFW is not initialized";
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(1024, 700, "Procedural Vegetation", nullptr,
                               nullptr);
    if (window_ == nullptr) {
        error = lastGlfwError.empty() ? "glfwCreateWindow failed"
                                      : lastGlfwError;
        return false;
    }
    return true;
}

bool GlfwSession::poll() {
    glfwPollEvents();
    return true;
}

bool GlfwSession::shouldClose() const {
    return window_ == nullptr || glfwWindowShouldClose(window_) == GLFW_TRUE;
}

void GlfwSession::setTitle(const std::string& title) {
    if (window_ != nullptr) {
        glfwSetWindowTitle(window_, title.c_str());
    }
}

void GlfwSession::waitEvents(double seconds) {
    if (window_ != nullptr && seconds > 0.0) {
        glfwWaitEventsTimeout(seconds);
    }
}

}  // namespace vegd
