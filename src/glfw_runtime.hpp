#pragma once

#include <string>

struct GLFWwindow;

namespace kfc {

class GlfwRuntime {
public:
  GlfwRuntime() = default;
  ~GlfwRuntime();

  bool initialize(bool openWindow, std::string* error);
  void setStatus(const std::string& status);
  void poll();
  bool shouldClose() const;

  bool initialized() const { return initialized_; }
  bool hasWindow() const { return window_ != nullptr; }
  const std::string& version() const { return version_; }
  const std::string& platform() const { return platform_; }
  const std::string& title() const { return title_; }

private:
  bool initialized_ = false;
  GLFWwindow* window_ = nullptr;
  std::string version_;
  std::string platform_;
  std::string title_ = "Skeletal keyframe compression - pause/resume lab";
};

}  // namespace kfc
