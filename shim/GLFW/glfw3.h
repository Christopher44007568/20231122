#pragma once
// Minimal GLFW 3.4 API-compatible shim header (headless fallback).
// Used only when a system GLFW is unavailable; CMake option
// TERRAIN_USE_SYSTEM_GLFW=ON switches to the real library instead.
// Only the subset of the API used by this experiment is declared.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLFW_FALSE 0
#define GLFW_TRUE 1

#define GLFW_NOT_INITIALIZED 0x00010001
#define GLFW_PLATFORM_ERROR 0x0001000E

#define GLFW_CLIENT_API 0x00022001
#define GLFW_NO_API 0
#define GLFW_PLATFORM 0x00050003
#define GLFW_PLATFORM_NULL 0x0006000E

typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;

int glfwInit(void);
void glfwTerminate(void);
void glfwInitHint(int hint, int value);
void glfwWindowHint(int hint, int value);
GLFWwindow* glfwCreateWindow(int width, int height, const char* title,
                             GLFWmonitor* monitor, GLFWwindow* share);
void glfwDestroyWindow(GLFWwindow* window);
int glfwVulkanSupported(void);
const char** glfwGetRequiredInstanceExtensions(uint32_t* count);
int glfwGetError(const char** description);
const char* glfwGetVersionString(void);

#ifdef __cplusplus
}
#endif
