#pragma once

#include "renderer.hpp"
#include "report.hpp"
#include "scene.hpp"
#include "session.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;

struct AppOptions {
  std::filesystem::path sessionDirectory = "sessions/current";
  std::filesystem::path shaderDirectory = "shaders";
  bool headless = false;
  bool runAll = false;
  bool fresh = false;
  int frameLimit = 0;
};

class Application {
 public:
  Application() = default;
  ~Application();

  bool initialize(const AppOptions& options, std::string& error);
  int run();
  void shutdown();

 private:
  static void keyCallback(GLFWwindow* window, int key, int scancode, int action,
                          int mods);
  static void mouseButtonCallback(GLFWwindow* window, int button, int action,
                                  int mods);
  static void cursorCallback(GLFWwindow* window, double x, double y);
  static void closeCallback(GLFWwindow* window);

  void onKey(int key, int action);
  void onMouseButton(int button, int action);
  void onCursor(double x, double y);
  bool runHeadless();
  bool initializeRenderer(std::string& error);
  bool rerunSelected(std::string& error);
  bool rerunObject(int index, bool exportEvidence, std::string& error);
  bool exportObjectEvidence(int index, std::string& error);
  bool ensureBaselineEvidence(std::string& error);
  void selectObjectAtCursor();
  void cyclePreset();
  void cycleView();
  void markDirty();
  bool saveCheckpoint(std::string& error);
  bool presentFrame(std::string& error);
  UiFrame buildUi() const;
  std::string statusLine(const ObjectRun& run) const;
  std::string formatFloat(float value, int precision = 3) const;
  const ObjectRun* selectedRun() const;
  ObjectRun* selectedRun();

  AppOptions options_;
  Scene scene_;
  SessionStore session_;
  Renderer renderer_;
  GLFWwindow* window_ = nullptr;
  int width_ = 1280;
  int height_ = 720;
  bool initialized_ = false;
  bool dirty_ = true;
  bool runningAll_ = false;
  bool glfwInitialized_ = false;
  FrameData extremeFrame_;
  FrameData correctedFrame_;
  std::vector<CandidateEvaluation> lastCandidates_;
  std::string overlayMessage_ = "Ready";
};

