#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app.hpp"

#include "image_io.hpp"

#include <chrono>
#include <iostream>
#include <thread>

Application::~Application() { shutdown(); }

bool Application::initialize(const AppOptions& options, std::string& error) {
  options_ = options;
  scene_ = Scene::makeDefault();
  if (!options_.headless) {
    if (glfwInit() != GLFW_TRUE) {
      error = "GLFW initialization failed";
      return false;
    }
    glfwInitialized_ = true;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window_ = glfwCreateWindow(1440, 810, "Screen Pick Depth Recheck",
                               nullptr, nullptr);
    if (window_ == nullptr) {
      error = "Cannot create GLFW window";
      return false;
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, keyCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetCursorPosCallback(window_, cursorCallback);
    glfwSetWindowCloseCallback(window_, closeCallback);
    glfwGetFramebufferSize(window_, &width_, &height_);
  }
  if (!session_.initialize(options_.sessionDirectory, options_.fresh, scene_, error)) {
    return false;
  }
  if (!initializeRenderer(error)) {
    return false;
  }
  if (!renderer_.renderBaseline(baselineParams(), error)) {
    error = "Baseline render failed: " + error;
    return false;
  }
  if (!ensureBaselineEvidence(error)) {
    return false;
  }
  if (!rerunSelected(error)) {
    error = "Initial object replay failed: " + error;
    return false;
  }
  initialized_ = true;
  overlayMessage_ = session_.state().resumed
    ? "Checkpoint restored; selected object replayed"
    : "Fresh session; selected object replayed";
  dirty_ = true;
  return true;
}

bool Application::initializeRenderer(std::string& error) {
  return renderer_.initialize(width_, height_, options_.headless, window_,
                              options_.shaderDirectory, error);
}

int Application::run() {
  if (options_.headless) {
    return runHeadless() ? 0 : 1;
  }
  if (options_.runAll) {
    runningAll_ = true;
    for (int index = 0; index < static_cast<int>(session_.state().runs.size());
         ++index) {
      if (glfwWindowShouldClose(window_) != 0) {
        break;
      }
      std::string error;
      if (!rerunObject(index, true, error)) {
        overlayMessage_ = "Run failed: " + error;
      }
      dirty_ = true;
      presentFrame(error);
      glfwPollEvents();
    }
    runningAll_ = false;
  }
  if (options_.frameLimit > 0) {
    for (int frame = 0; frame < options_.frameLimit; ++frame) {
      glfwPollEvents();
      std::string error;
      if (!presentFrame(error)) {
        std::cerr << "Frame presentation failed: " << error << "\n";
        return 1;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    std::string saveError;
    if (!saveCheckpoint(saveError)) {
      std::cerr << "Checkpoint save failed: " << saveError << "\n";
      return 1;
    }
    std::cout << "Presented " << options_.frameLimit << " frame(s)\n";
    shutdown();
    return 0;
  }
  while (glfwWindowShouldClose(window_) == 0) {
    glfwPollEvents();
    if (dirty_) {
      std::string error;
      if (!presentFrame(error)) {
        overlayMessage_ = "Present failed: " + error;
      }
      dirty_ = false;
    }
    glfwWaitEventsTimeout(0.15);
  }
  std::string saveError;
  if (!saveCheckpoint(saveError)) {
    std::cerr << "Checkpoint save failed: " << saveError << "\n";
    return 1;
  }
  shutdown();
  return 0;
}

bool Application::runHeadless() {
  for (int index = 0; index < static_cast<int>(session_.state().runs.size());
       ++index) {
    std::string error;
    if (!rerunObject(index, true, error)) {
      std::cerr << "Object " << index << " failed: " << error << "\n";
      return false;
    }
    const ObjectRun& run = session_.state().runs[static_cast<std::size_t>(index)];
    std::cout << "[" << index + 1 << "/" << session_.state().runs.size() << "] "
              << run.name << " extreme="
              << run.extreme.compare.classification
              << " missing=" << run.extreme.compare.missingPixels
              << " corrected=" << run.corrected.compare.classification
              << " missing=" << run.corrected.compare.missingPixels << "\n";
  }
  std::string error;
  if (!saveCheckpoint(error)) {
    std::cerr << "Checkpoint save failed: " << error << "\n";
    return false;
  }
  std::cout << "Session: " << session_.root().string() << "\n";
  std::cout << "Evidence: " << session_.root().string() << "/evidence\n";
  shutdown();
  return true;
}

bool Application::presentFrame(std::string& error) {
  if (options_.headless) {
    return true;
  }
  const ObjectRun* run = selectedRun();
  const std::uint32_t focusId = run == nullptr ? 0 : run->id;
  const float split = 0.5f;
  return renderer_.present(session_.state().viewMode, focusId, split,
                           buildUi(), error);
}

bool Application::saveCheckpoint(std::string& error) {
  return session_.save(error);
}

void Application::shutdown() {
  if (options_.headless) {
    renderer_.shutdown();
    return;
  }
  if (initialized_) {
    std::string error;
    saveCheckpoint(error);
  }
  renderer_.shutdown();
  if (window_ != nullptr) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  if (glfwInitialized_) {
    glfwTerminate();
    glfwInitialized_ = false;
  }
  initialized_ = false;
}

void Application::keyCallback(GLFWwindow* window, int key, int, int action,
                              int) {
  auto* application =
    static_cast<Application*>(glfwGetWindowUserPointer(window));
  if (application != nullptr) {
    application->onKey(key, action);
  }
}

void Application::mouseButtonCallback(GLFWwindow* window, int button,
                                      int action, int) {
  auto* application =
    static_cast<Application*>(glfwGetWindowUserPointer(window));
  if (application != nullptr && button == GLFW_MOUSE_BUTTON_LEFT &&
      action == GLFW_PRESS) {
    application->onMouseButton(button, action);
  }
}

void Application::cursorCallback(GLFWwindow* window, double x, double y) {
  auto* application =
    static_cast<Application*>(glfwGetWindowUserPointer(window));
  if (application != nullptr) {
    application->onCursor(x, y);
  }
}

void Application::closeCallback(GLFWwindow* window) {
  auto* application =
    static_cast<Application*>(glfwGetWindowUserPointer(window));
  if (application != nullptr) {
    std::string error;
    if (!application->saveCheckpoint(error)) {
      std::cerr << "Checkpoint save on close failed: " << error << "\n";
    }
  }
}

