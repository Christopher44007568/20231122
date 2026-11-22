#define GLFW_INCLUDE_NONE
#include "glfw_runtime.hpp"

#include <GLFW/glfw3.h>

#include <sstream>

namespace kfc {

namespace {

std::string lastGlfwError;

void errorCallback(int code, const char* description) {
  std::ostringstream out;
  out << "GLFW error " << code << ": " << (description == nullptr ? "" : description);
  lastGlfwError = out.str();
}

const char* platformName(int platform) {
  switch (platform) {
    case GLFW_PLATFORM_WIN32: return "win32";
    case GLFW_PLATFORM_COCOA: return "cocoa";
    case GLFW_PLATFORM_WAYLAND: return "wayland";
    case GLFW_PLATFORM_X11: return "x11";
    case GLFW_PLATFORM_NULL: return "null";
    default: return "unknown";
  }
}

}  // namespace

GlfwRuntime::~GlfwRuntime() {
  if (window_ != nullptr) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  if (initialized_) {
    glfwTerminate();
    initialized_ = false;
  }
}

bool GlfwRuntime::initialize(bool openWindow, std::string* error) {
  lastGlfwError.clear();
  glfwSetErrorCallback(errorCallback);
  if (!openWindow) {
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
  }
  if (glfwInit() != GLFW_TRUE) {
    if (error != nullptr) {
      *error = lastGlfwError.empty() ? "glfwInit failed" : lastGlfwError;
    }
    return false;
  }
  initialized_ = true;
  version_ = glfwGetVersionString();
  platform_ = platformName(glfwGetPlatform());
  if (!openWindow) return true;

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  window_ = glfwCreateWindow(1120, 680, title_.c_str(), nullptr, nullptr);
  if (window_ == nullptr) {
    if (error != nullptr) {
      *error = lastGlfwError.empty() ? "glfwCreateWindow failed" : lastGlfwError;
    }
    glfwTerminate();
    initialized_ = false;
    return false;
  }
  return true;
}

void GlfwRuntime::setStatus(const std::string& status) {
  title_ = status;
  if (window_ != nullptr) glfwSetWindowTitle(window_, title_.c_str());
}

void GlfwRuntime::poll() {
  if (initialized_) glfwPollEvents();
}

bool GlfwRuntime::shouldClose() const {
  return window_ != nullptr && glfwWindowShouldClose(window_) == GLFW_TRUE;
}

}  // namespace kfc
