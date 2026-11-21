#include "app/ui.hpp"
#include "app/vulkan_renderer.hpp"
#include "core/export.hpp"
#include "core/hash.hpp"
#include "core/io.hpp"
#include "core/session.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

struct InputState {
    vfcr::UiApp* ui = nullptr;
    GLFWwindow* window = nullptr;
};

void framebufferScale(GLFWwindow* window, double x, double y, double& scaledX,
                      double& scaledY) {
    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    scaledX = windowWidth > 0 ? x * framebufferWidth / windowWidth : x;
    scaledY = windowHeight > 0 ? y * framebufferHeight / windowHeight : y;
}

void cursorCallback(GLFWwindow* window, double x, double y) {
    auto* state = static_cast<InputState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr || state->ui == nullptr) {
        return;
    }
    double sx = 0.0;
    double sy = 0.0;
    framebufferScale(window, x, y, sx, sy);
    state->ui->pointerMove(sx, sy);
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    (void)mods;
    auto* state = static_cast<InputState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr || state->ui == nullptr || button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    double sx = 0.0;
    double sy = 0.0;
    framebufferScale(window, x, y, sx, sy);
    if (action == GLFW_PRESS) {
        state->ui->pointerDown(sx, sy);
    } else if (action == GLFW_RELEASE) {
        state->ui->pointerUp();
    }
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    auto* state = static_cast<InputState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr || state->ui == nullptr) {
        return;
    }
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        state->ui->keyDown(key, mods);
    }
}

void scrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
    (void)xOffset;
    auto* state = static_cast<InputState*>(glfwGetWindowUserPointer(window));
    if (state != nullptr && state->ui != nullptr) {
        state->ui->scroll(yOffset);
    }
}

void printUsage() {
    std::cout
        << "VFC Replay / Local Correction Workbench\n"
        << "usage: vfcr [options]\n\n"
        << "  --fresh          start a new trace instead of restoring the session\n"
        << "  --state <path>   session path (default: .vfcr/session.json)\n"
        << "  --open <path>    reload and verify an exported record.json\n"
        << "  --snapshot <path> render one Vulkan frame to PPM, then exit\n"
        << "  --help           show this help\n";
}

}  // namespace

int main(int argc, char** argv) {
    bool fresh = false;
    std::string snapshotPath;
    std::string statePath = vfcr::defaultStatePath();
    std::string openPath;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--fresh") {
            fresh = true;
        } else if (argument == "--state" && i + 1 < argc) {
            statePath = argv[++i];
        } else if (argument == "--open" && i + 1 < argc) {
            openPath = argv[++i];
        } else if (argument == "--snapshot" && i + 1 < argc) {
            snapshotPath = argv[++i];
        } else if (argument == "--help" || argument == "-h") {
            printUsage();
            return 0;
        } else {
            std::cerr << "unknown argument: " << argument << "\n";
            printUsage();
            return 2;
        }
    }

    vfcr::Session session;
    std::string startupMessage;
    std::string error;
    if (!openPath.empty()) {
        if (vfcr::loadExportRecord(openPath, session, &error)) {
            startupMessage = "RECORD RELOADED / FIELDS+ORDER VERIFIED / " +
                             vfcr::shortDigest(vfcr::evidenceHead(session.evidence), 12);
            vfcr::appendEvidence(session, "RECORD_REOPENED", "",
                                 "ARTIFACT " + openPath + " / ORDER VERIFIED",
                                 vfcr::nowIso8601Utc());
            vfcr::saveSessionFile(statePath, session, nullptr);
        } else {
            std::cerr << "warning: cannot reload record: " << error << "\n";
        }
    }
    if (session.objects.empty() && !fresh && vfcr::pathExists(statePath)) {
        if (vfcr::loadSessionFile(statePath, session, &error)) {
            int runs = 0;
            for (const auto& object : session.objects) {
                runs += static_cast<int>(object.runs.size());
            }
            startupMessage = "SESSION RESTORED / " + std::to_string(runs) +
                             " RUNS / PLAYHEAD " + std::to_string(session.playhead);
            vfcr::appendEvidence(session, "SESSION_RESTORED", "",
                                 "RESUME FROM ATOMIC SESSION / " + std::to_string(runs) + " RUNS",
                                 vfcr::nowIso8601Utc());
        } else {
            std::cerr << "warning: cannot restore session: " << error << "\n";
        }
    }
    if (session.objects.empty()) {
        session = vfcr::makeDefaultSession();
        if (startupMessage.empty()) {
            startupMessage = "NEW TRACE / 6 OBJECTS / GAP-017 READY";
        }
    }

    if (std::getenv("VFCR_X11") != nullptr) {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    }
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "glfwInit failed\n";
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1240, 760, "VFC Replay / Local Correction",
                                          nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "glfwCreateWindow failed\n";
        glfwTerminate();
        return 1;
    }
    glfwSetWindowSizeLimits(window, 1100, 700, GLFW_DONT_CARE, GLFW_DONT_CARE);
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor(); monitor != nullptr) {
        if (const GLFWvidmode* mode = glfwGetVideoMode(monitor); mode != nullptr) {
            glfwSetWindowPos(window, std::max(0, (mode->width - 1240) / 2),
                             std::max(0, (mode->height - 760) / 2));
        }
    }

    int exitCode = 0;
    try {
        {
            vfcr::VulkanRenderer renderer(window);
            vfcr::UiApp ui(vfcr::Session(session), statePath, startupMessage);
            if (!snapshotPath.empty()) {
                ui.requestSnapshot(snapshotPath);
            }
            InputState input{&ui, window};
            glfwSetWindowUserPointer(window, &input);
            glfwSetCursorPosCallback(window, cursorCallback);
            glfwSetMouseButtonCallback(window, mouseButtonCallback);
            glfwSetKeyCallback(window, keyCallback);
            glfwSetScrollCallback(window, scrollCallback);

            auto previous = std::chrono::steady_clock::now();
            while (glfwWindowShouldClose(window) == GLFW_FALSE && !ui.exitRequested()) {
                glfwPollEvents();
                const auto now = std::chrono::steady_clock::now();
                const double delta =
                    std::chrono::duration<double>(now - previous).count();
                previous = now;
                ui.tick(std::min(0.1, delta));

                int width = 0;
                int height = 0;
                glfwGetFramebufferSize(window, &width, &height);
                if (width <= 0 || height <= 0) {
                    glfwWaitEventsTimeout(0.05);
                    continue;
                }
                const vfcr::UiFrame frame = ui.buildFrame(width, height);
                const vfcr::CaptureResult capture =
                    renderer.draw(frame.vertices, frame.capture ? frame.capturePath : "");
                if (frame.capture) {
                    ui.finishExport(capture);
                }
                if (!snapshotPath.empty() && ui.snapshotDone()) {
                    break;
                }
                const auto frameEnd = std::chrono::steady_clock::now();
                const auto frameDuration =
                    std::chrono::duration<double>(frameEnd - now).count();
                if (frameDuration < 0.016) {
                    std::this_thread::sleep_for(
                        std::chrono::duration<double>(0.016 - frameDuration));
                }
            }
            ui.saveNow();
            std::cout << "session saved: " << statePath << "\n";
        }
    } catch (const std::exception& exception) {
        std::cerr << "fatal: " << exception.what() << "\n";
        exitCode = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
