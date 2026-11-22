#pragma once
#include "renderer.h"

// Opens a GLFW window and presents the CSM visualization via a Vulkan
// swapchain until the window is closed or the timeout elapses.
// Returns 0 on success, 2 on error (message printed to stderr).
int runPresent(const SceneParams& params, int timeoutSeconds);
