#include "gbi/commands.hpp"

#include "gbi/compare.hpp"
#include "gbi/renderer.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <deque>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace gbi {

namespace {

GLFWwindow* g_window = nullptr;

struct KeyEvent {
  int key = 0;
  int mods = 0;
};

std::deque<KeyEvent> g_key_events;

void errorCallback(int code, const char* description) {
  std::cerr << "GLFW error " << code << ": "
            << (description == nullptr ? "unknown" : description) << "\n";
}

void keyCallback(GLFWwindow*, int key, int, int action, int mods) {
  if (action == GLFW_PRESS) {
    g_key_events.push_back({key, mods});
  }
}

std::string windowTitle(const SessionState& session,
                        const std::vector<SceneObject>& objects,
                        const std::string& selected_object, bool isolate) {
  std::ostringstream title;
  title << "GBuffer Inspector | object=" << selected_object;
  for (std::size_t index = 0; index < objects.size() && index < 9; ++index) {
    if (objects[index].id == selected_object) {
      title << " [" << (index + 1) << "]";
      break;
    }
  }
  title << " mode=" << (isolate ? "isolate" : "scene");
  title << " | A0=" << session.get("anchor.A0.latest", "-")
        << " A1=" << session.get("object." + sanitizeName(selected_object) +
                                  ".A1.latest", "-")
        << " A2=" << session.get("anchor.A2.latest", "-")
        << " | 1-3 select  F scene/isolate  F5 A0  F6 A1  F7 A2"
           "  E export  Q quit";
  return title.str();
}

std::string captureAnchorForAction(SessionState& session,
                                   const std::string& anchor,
                                   const std::string& object_id,
                                   const Workspace& workspace) {
  CaptureSession capture_session(workspace);
  const CaptureEvidence capture =
      capture_session.capture(anchor, object_id, "gui", "captured from GLFW",
                               640, 360);
  session = workspace.loadSession();
  return capture.capture_id;
}

}  // namespace

int commandRun(const CommonOptions& options) {
  Workspace workspace = Workspace::ensure(options.workspace);
  {
    WorkspaceLock guard(workspace);
    workspace.recover();
  }
  glfwSetErrorCallback(errorCallback);
  if (glfwInit() != GLFW_TRUE) {
    throw std::runtime_error(
        "GLFW initialization failed; use CLI capture for headless operation");
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  g_window = glfwCreateWindow(1280, 720, "GBuffer Inspector",
                              nullptr, nullptr);
  if (g_window == nullptr) {
    glfwTerminate();
    throw std::runtime_error("GLFW window creation failed");
  }

  try {
    VulkanRenderer renderer(640, 360, g_window);
    SessionState session = workspace.loadSession();
    Scene scene = workspace.loadScene();
    std::string selected = session.get("selected_object");
    if (scene.find(selected) == nullptr && !scene.objects.empty()) {
      selected = scene.objects.front().id;
    }
    bool dirty = true;
    bool isolate = true;
    std::string status = "ready";
    glfwSetKeyCallback(g_window, keyCallback);
    glfwSetWindowTitle(
        g_window,
        windowTitle(session, scene.objects, selected, isolate).c_str());
    while (glfwWindowShouldClose(g_window) == GLFW_FALSE) {
      glfwWaitEventsTimeout(1.0 / 30.0);
      if (InterruptGuard::requested()) {
        glfwSetWindowShouldClose(g_window, GLFW_TRUE);
      }
      while (!g_key_events.empty()) {
        const KeyEvent event = g_key_events.front();
        g_key_events.pop_front();
        if (event.key >= GLFW_KEY_1 && event.key <= GLFW_KEY_9) {
          const std::size_t index =
              static_cast<std::size_t>(event.key - GLFW_KEY_1);
          if (index < scene.objects.size()) {
            selected = scene.objects[index].id;
            dirty = true;
          }
        } else if (event.key == GLFW_KEY_TAB) {
          const auto current = scene.indexOf(selected);
          const std::size_t next =
              current.has_value() ? (*current + 1) % scene.objects.size() : 0;
          selected = scene.objects[next].id;
          dirty = true;
        } else if (event.key == GLFW_KEY_F) {
          isolate = !isolate;
          dirty = true;
        } else if (event.key == GLFW_KEY_ESCAPE || event.key == GLFW_KEY_Q) {
          glfwSetWindowShouldClose(g_window, GLFW_TRUE);
        } else if (event.key == GLFW_KEY_F5) {
          try {
            status = "captured A0 " +
                     captureAnchorForAction(session, "A0", "", workspace);
            scene = workspace.loadScene();
            dirty = true;
          } catch (const std::exception& error) {
            status = std::string("A0 error: ") + error.what();
          }
        } else if (event.key == GLFW_KEY_F6) {
          try {
            status =
                "captured A1 " +
                captureAnchorForAction(session, "A1", selected, workspace);
            scene = workspace.loadScene();
            dirty = true;
          } catch (const std::exception& error) {
            status = std::string("A1 error: ") + error.what();
          }
        } else if (event.key == GLFW_KEY_F7) {
          try {
            status = "captured A2 " +
                     captureAnchorForAction(session, "A2", "", workspace);
            scene = workspace.loadScene();
            dirty = true;
          } catch (const std::exception& error) {
            status = std::string("A2 error: ") + error.what();
          }
        } else if (event.key == GLFW_KEY_E) {
          try {
            const bool shift = (event.mods & GLFW_MOD_SHIFT) != 0;
            const ExportResult result = exportObjectComparison(
                workspace, selected, "A0", shift ? "A2" : "A1");
            status = "exported " + result.directory.filename().string();
          } catch (const std::exception& error) {
            status = std::string("export error: ") + error.what();
          }
        } else if (event.key == GLFW_KEY_S) {
          try {
            const VerificationReport report = verifyWorkspace(workspace);
            status = report.ok ? "verify PASS" : "verify FAIL";
          } catch (const std::exception& error) {
            status = std::string("verify error: ") + error.what();
          }
        }
      }
      if (dirty) {
        std::vector<std::string> visible;
        std::uint32_t object_index = 0;
        if (isolate) {
          visible.push_back(selected);
          object_index = static_cast<std::uint32_t>(
              scene.indexOf(selected).value_or(0) + 1);
        } else {
          for (const SceneObject& object : scene.objects) {
            visible.push_back(object.id);
          }
        }
        RenderedView view = renderer.render(
            scene, visible, "gui.preview", isolate ? "object" : "scene",
            isolate ? selected : "", object_index, 0);
        (void)view;
        renderer.present();
        glfwSetWindowTitle(
            g_window,
            (windowTitle(session, scene.objects, selected, isolate) + " | " +
             status)
                .c_str());
        dirty = false;
      }
    }
    {
      WorkspaceLock guard(workspace);
      workspace.mutateSession([&](SessionState& state) {
        state.set("selected_object", selected);
        state.set("clean_exit", "1");
        state.set("last_action", "gui.exit");
      });
      Json fields = Json::object();
      fields["selected_object"] = selected;
      fields["clean_exit"] = true;
      workspace.journal("gui.exit", fields);
    }
  } catch (...) {
    glfwDestroyWindow(g_window);
    g_window = nullptr;
    glfwTerminate();
    throw;
  }
  glfwDestroyWindow(g_window);
  g_window = nullptr;
  glfwTerminate();
  return 0;
}

}  // namespace gbi
