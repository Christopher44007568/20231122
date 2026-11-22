#pragma once

#include <string>

struct GLFWwindow;

namespace vegd {

class GlfwSession {
public:
    GlfwSession() = default;
    ~GlfwSession();
    GlfwSession(const GlfwSession&) = delete;
    GlfwSession& operator=(const GlfwSession&) = delete;

    bool initialize(bool interactive, std::string& error);
    bool createWindow(std::string& error);
    bool poll();
    bool shouldClose() const;
    void setTitle(const std::string& title);
    void waitEvents(double seconds);
    bool interactive() const { return interactive_; }
    const std::string& version() const { return version_; }

private:
    bool initialized_ = false;
    bool interactive_ = false;
    GLFWwindow* window_ = nullptr;
    std::string version_;
};

}  // namespace vegd
