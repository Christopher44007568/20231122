#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "../core/analyzer.hpp"
#include "../core/parser.hpp"
#include "inspection_page.hpp"
#include "page_surface.hpp"
#include "vulkan_renderer.hpp"

#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace gcdump;

namespace {

void writePpm(const std::string& path, const gui::Bitmap& bm) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << bm.width << ' ' << bm.height << "\n255\n";
    for (size_t i = 0; i < bm.pixels.size(); i += 4)
        out.put(static_cast<char>(bm.pixels[i]))
            .put(static_cast<char>(bm.pixels[i + 1]))
            .put(static_cast<char>(bm.pixels[i + 2]));
}

} // namespace

int main(int argc, char** argv) {
    std::string dumpPath = "samples/recovered_dump.gcdump";
    std::string screenshotPath;
    int maxFrames = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            screenshotPath = argv[++i];
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            maxFrames = std::atoi(argv[++i]);
        else if (argv[i][0] != '-')
            dumpPath = argv[i];
    }

    std::ifstream file(dumpPath, std::ios::binary);
    if (!file) {
        std::cerr << "cannot open dump: " << dumpPath << "\n";
        return 3;
    }
    Report report = analyze(parseDump(file), dumpPath);
    gui::InspectionPage page(std::move(report));

    if (!screenshotPath.empty()) {
        writePpm(screenshotPath, page.bitmap());
        std::cout << "screenshot written: " << screenshotPath << " ("
                  << page.bitmap().width << "x" << page.bitmap().height
                  << ")\n";
        return 0;
    }

    if (!glfwInit()) {
        std::cerr << "glfwInit failed\n";
        return 3;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(
        1180, 760, "GPU crash dump provenance", nullptr, nullptr);
    if (!window) {
        std::cerr << "glfwCreateWindow failed\n";
        glfwTerminate();
        return 3;
    }

    try {
        gui::VulkanRenderer renderer(window);
        renderer.setClearColor(page.statusColor());
        gui::PageSurface surface(renderer.physicalDevice(),
                                 renderer.device());
        const auto& bm = page.bitmap();
        surface.upload(bm.pixels.data(), bm.width, bm.height);
        renderer.setOutsidePassCallback(
            [&](VkCommandBuffer cmd, VkImage target, VkExtent2D extent) {
                surface.blitTo(cmd, target, extent);
            });

        int frameCount = 0;
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            renderer.drawFrame(nullptr);
            if (maxFrames >= 0 && ++frameCount >= maxFrames) break;
        }
        renderer.waitIdle();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 2;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
