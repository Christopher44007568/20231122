#include "renderer.h"
#include "scene.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

static std::string triggerString(uint32_t flags) {
    std::string t;
    if (flags & 1) t += "depth";
    if (flags & 2) t += (t.empty() ? "" : "+") + std::string("normal");
    return t.empty() ? "none" : t;
}

static std::string suppressString(uint32_t flags) {
    std::string s;
    if (flags & 4) s += "depth_threshold";
    if (flags & 8) s += (s.empty() ? "" : "+") + std::string("normal_threshold");
    return s.empty() ? "none" : s;
}

static void writeDiagReport(std::ostream& os, const char* sceneName,
                            const FrameParams& params,
                            const std::vector<DiagRecord>& recs) {
    os << "scene=" << sceneName << " depth_threshold=" << params.depthThreshold
       << " normal_threshold=" << params.normalThreshold
       << " gt_eps(depth=" << params.gtDepthEps << ", normal=" << params.gtNormalEps
       << ")\n";
    os << "broken_pixels=" << recs.size() << "\n";
    for (const DiagRecord& r : recs) {
        os << "pixel(" << r.x << "," << r.y << ")"
           << " trigger=" << triggerString(r.flags)
           << " suppressed_by=" << suppressString(r.flags)
           << " depthDiff=" << r.depthDiff << " (thr " << r.depthThreshold << ")"
           << " normalDiff=" << r.normalDiff << " (thr " << r.normalThreshold << ")"
           << " dz[L,R,D,U]=[" << r.dz[0] << "," << r.dz[1] << "," << r.dz[2] << ","
           << r.dz[3] << "]"
           << " dn[L,R,D,U]=[" << r.dn[0] << "," << r.dn[1] << "," << r.dn[2] << ","
           << r.dn[3] << "]\n";
    }
}

static int runHeadless(const std::string& outDir, FrameParams base) {
    Renderer renderer;
    renderer.init(nullptr, 1280, 960);
    SceneSet scenes = buildScenes();
    renderer.uploadScene(scenes);
    for (size_t i = 0; i < scenes.scenes.size(); ++i) {
        FrameParams params = base;
        params.sceneIndex = (int)i;
        params.runDiagnostics = true;
        renderer.renderFrame(scenes, params);
        std::string base = outDir + "/" + scenes.scenes[i].name;
        renderer.saveFramePPM(base + ".ppm");
        std::ofstream txt(base + "_diag.txt");
        writeDiagReport(txt, scenes.scenes[i].name, params, renderer.diagRecords());
        writeDiagReport(std::cout, scenes.scenes[i].name, params,
                        renderer.diagRecords());
        std::printf("wrote %s.ppm and %s_diag.txt (%zu broken pixels)\n",
                    base.c_str(), base.c_str(), renderer.diagRecords().size());
    }
    renderer.shutdown();
    return 0;
}

int main(int argc, char** argv) {
    FrameParams cliParams;
    std::string dumpDir;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](float* out) {
            if (i + 1 < argc) *out = std::stof(argv[++i]);
        };
        if (arg == "--dump" && i + 1 < argc)
            dumpDir = argv[++i];
        else if (arg == "--depth-thr")
            next(&cliParams.depthThreshold);
        else if (arg == "--normal-thr")
            next(&cliParams.normalThreshold);
        else if (arg == "--gt-depth-eps")
            next(&cliParams.gtDepthEps);
        else if (arg == "--gt-normal-eps")
            next(&cliParams.gtNormalEps);
    }
    if (!dumpDir.empty())
        return runHeadless(dumpDir, cliParams);

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 960, "outline_diag", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "window creation failed\n");
        glfwTerminate();
        return 1;
    }

    Renderer renderer;
    renderer.init(window, 1280, 960);
    SceneSet scenes = buildScenes();
    renderer.uploadScene(scenes);

    FrameParams params;
    bool needFrame = true;
    auto updateTitle = [&]() {
        char title[256];
        std::snprintf(title, sizeof(title),
                      "outline_diag | scene=%s [Tab] | depthThr=%.4f [1/2] | "
                      "normalThr=%.4f [3/4] | P=dump diag",
                      scenes.scenes[params.sceneIndex].name, params.depthThreshold,
                      params.normalThreshold);
        glfwSetWindowTitle(window, title);
    };
    updateTitle();

    while (!renderer.windowShouldClose()) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;

        static bool keysDown[8] = {};
        auto edge = [&](int key, int slot) {
            bool down = glfwGetKey(window, key) == GLFW_PRESS;
            bool pressed = down && !keysDown[slot];
            keysDown[slot] = down;
            return pressed;
        };
        if (edge(GLFW_KEY_1, 0)) params.depthThreshold /= 1.25f;
        if (edge(GLFW_KEY_2, 1)) params.depthThreshold *= 1.25f;
        if (edge(GLFW_KEY_3, 2)) params.normalThreshold /= 1.25f;
        if (edge(GLFW_KEY_4, 3)) params.normalThreshold *= 1.25f;
        if (edge(GLFW_KEY_TAB, 4) || edge(GLFW_KEY_S, 5))
            params.sceneIndex = (params.sceneIndex + 1) % (int)scenes.scenes.size();
        bool dump = edge(GLFW_KEY_P, 6);
        updateTitle();

        params.runDiagnostics = dump;
        renderer.renderFrame(scenes, params);
        if (dump) {
            writeDiagReport(std::cout, scenes.scenes[params.sceneIndex].name, params,
                            renderer.diagRecords());
            std::string path = std::string("diag_") +
                               scenes.scenes[params.sceneIndex].name + ".txt";
            std::ofstream f(path);
            writeDiagReport(f, scenes.scenes[params.sceneIndex].name, params,
                            renderer.diagRecords());
            std::printf("dumped %zu broken pixels to %s\n",
                        renderer.diagRecords().size(), path.c_str());
        }
        (void)needFrame;
    }

    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
