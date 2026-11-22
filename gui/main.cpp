#include <algorithm>
#include <iostream>
#include <memory>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "camtrak/engine.hpp"
#include "camtrak/interpolate.hpp"
#include "camtrak/model.hpp"
#include "camtrak/store.hpp"
#include "vulkan_app.hpp"

using namespace camtrak;

namespace {

struct GuiState {
  std::unique_ptr<Store> store;
  std::vector<Trajectory> trajectories;
  std::vector<std::string> object_ids;
  std::size_t selected = 0;
  InterpMethod method = InterpMethod::Linear;
  double gap_t0 = 2.0;
  double gap_t1 = 3.0;
  double period = 0.1;
  std::string status_line;
};

gui::RenderLine trajectoryLine(const Trajectory& trajectory, float r, float g,
                               float b) {
  gui::RenderLine line;
  line.r = r;
  line.g = g;
  line.b = b;
  for (const Sample& sample : trajectory.samples) {
    line.points.push_back(sample.pose.position);
  }
  return line;
}

std::vector<gui::RenderLine> buildLines(const GuiState& state) {
  std::vector<gui::RenderLine> lines;
  for (std::size_t i = 0; i < state.trajectories.size(); ++i) {
    const bool selected = i == state.selected;
    const Trajectory& raw = state.trajectories[i];
    const Trajectory mapped = state.store->toReferenceTimeline(raw);
    gui::RenderLine base =
        trajectoryLine(mapped, selected ? 0.85f : 0.35f,
                       selected ? 0.85f : 0.35f,
                       selected ? 0.95f : 0.40f);
    lines.push_back(base);

    if (!selected) continue;
    // Highlight the missing interval in red using boundary samples.
    gui::RenderLine gap;
    gap.r = 1.0f;
    gap.g = 0.25f;
    gap.b = 0.20f;
    for (const Sample& sample : mapped.samples) {
      if (sample.t_device >= state.gap_t0 - 0.05 &&
          sample.t_device <= state.gap_t1 + 0.05) {
        gap.points.push_back(sample.pose.position);
      }
    }
    if (gap.points.size() >= 2) lines.push_back(gap);

    // Preview the corrected segment in green without persisting a run.
    CorrectionConfig config;
    config.object_id = state.object_ids[state.selected];
    config.gap_t0 = state.gap_t0;
    config.gap_t1 = state.gap_t1;
    config.sample_period = state.period;
    config.method = state.method;
    try {
      std::vector<GapSegment> corrected =
          fillGap(mapped.samples, config);
      gui::RenderLine preview;
      preview.r = 0.25f;
      preview.g = 1.0f;
      preview.b = 0.45f;
      for (const GapSegment& segment : corrected) {
        preview.points.push_back(segment.pose.position);
      }
      lines.push_back(preview);
    } catch (const std::exception&) {
      // Gap outside the sampled range: skip the preview rather than crash.
    }
  }
  return lines;
}

void onKey(GLFWwindow* window, int key, int, int action, int) {
  if (action != GLFW_PRESS) return;
  auto* state = static_cast<GuiState*>(glfwGetWindowUserPointer(window));
  if (key == GLFW_KEY_ESCAPE) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
  } else if (key == GLFW_KEY_TAB) {
    state->selected = (state->selected + 1) % state->object_ids.size();
    state->status_line =
        "selected object: " + state->object_ids[state->selected];
  } else if (key == GLFW_KEY_M) {
    state->method = state->method == InterpMethod::Linear
                        ? InterpMethod::Cubic
                        : InterpMethod::Linear;
    state->status_line =
        std::string("method: ") + methodName(state->method);
  } else if (key == GLFW_KEY_R) {
    Engine engine(*state->store);
    CorrectionConfig config;
    config.object_id = state->object_ids[state->selected];
    config.gap_t0 = state->gap_t0;
    config.gap_t1 = state->gap_t1;
    config.sample_period = state->period;
    config.method = state->method;
    try {
      const EngineReport report = engine.runObject(config);
      state->status_line = "recorded run " + report.result.run_id;
    } catch (const std::exception& error) {
      state->status_line = std::string("run failed: ") + error.what();
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: camtrak_gui <project_dir>\n";
    return 2;
  }
  GuiState state;
  try {
    state.store = std::make_unique<Store>(argv[1]);
    const Project project = state.store->loadProject();
    state.object_ids = project.object_ids;
    for (const std::string& object_id : state.object_ids) {
      state.trajectories.push_back(state.store->loadTrajectory(object_id));
    }
    if (state.object_ids.empty()) {
      std::cerr << "project contains no render objects\n";
      return 1;
    }
  } catch (const std::exception& error) {
    std::cerr << "failed to open project: " << error.what() << "\n";
    return 1;
  }

  if (!glfwInit()) {
    std::cerr << "GLFW initialization failed\n";
    return 1;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window =
      glfwCreateWindow(1280, 800, "camtrak trajectory review", nullptr,
                       nullptr);
  if (window == nullptr) {
    std::cerr << "failed to create window (no display server?)\n";
    glfwTerminate();
    return 1;
  }
  glfwSetWindowUserPointer(window, &state);
  glfwSetKeyCallback(window, onKey);

  gui::VulkanApp app;
  if (!app.initialize(window)) {
    std::cerr << "Vulkan unavailable; use the camtrak CLI in headless "
                 "environments\n";
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    app.drawFrame(buildLines(state));
    if (const char* auto_quit = std::getenv("CAMTRAK_AUTO_QUIT_FRAMES")) {
      static int remaining = std::atoi(auto_quit);
      if (--remaining <= 0) glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
  }
  app.shutdown();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
