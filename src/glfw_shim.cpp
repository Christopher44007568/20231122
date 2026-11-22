// Headless GLFW shim implementation. Mirrors GLFW 3.4 semantics for the
// subset used here: init succeeds, window creation reports GLFW_PLATFORM_ERROR
// (no display in this environment), Vulkan support is probed via the loader.
#include <GLFW/glfw3.h>
#include <dlfcn.h>
#include <string>

namespace {
bool g_initialized = false;
int g_errorCode = 0;
std::string g_errorDesc;

void setError(int code, const std::string& desc) {
    g_errorCode = code;
    g_errorDesc = desc;
}
} // namespace

int glfwInit(void) {
    g_initialized = true;
    g_errorCode = 0;
    g_errorDesc.clear();
    return GLFW_TRUE;
}

void glfwTerminate(void) {
    g_initialized = false;
}

void glfwInitHint(int, int) {}

void glfwWindowHint(int, int) {}

GLFWwindow* glfwCreateWindow(int, int, const char*, GLFWmonitor*, GLFWwindow*) {
    if (!g_initialized) {
        setError(GLFW_NOT_INITIALIZED, "GLFW shim: glfwInit was not called");
        return nullptr;
    }
    setError(GLFW_PLATFORM_ERROR,
             "GLFW shim: headless environment, window creation unsupported");
    return nullptr;
}

void glfwDestroyWindow(GLFWwindow*) {}

int glfwVulkanSupported(void) {
    if (!g_initialized) return GLFW_FALSE;
    void* lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return GLFW_FALSE;
    void* sym = dlsym(lib, "vkGetInstanceProcAddr");
    dlclose(lib);
    return sym ? GLFW_TRUE : GLFW_FALSE;
}

const char** glfwGetRequiredInstanceExtensions(uint32_t* count) {
    if (count) *count = 0; // null platform: no surface extensions required
    return nullptr;
}

int glfwGetError(const char** description) {
    if (description) *description = g_errorDesc.empty() ? nullptr : g_errorDesc.c_str();
    int code = g_errorCode;
    g_errorCode = 0;
    return code;
}

const char* glfwGetVersionString(void) {
    return "3.4.0-shim-headless";
}
