#include "experiment.h"
#include "vk_context.h"
#include "journal.h"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
using journal::join;
using journal::kv;

namespace {

struct Bootstrap {
    bool glfwWindowOk = false;
    bool vulkanOk = false;
    std::string deviceName;
};

// Records environment/bootstrap steps into bootstrap.jsonl (evidence).
Bootstrap bootstrap(VkContext& vk, const std::string& evidenceDir) {
    Bootstrap b;
    Journal j;
    j.open(evidenceDir + "/bootstrap.jsonl");
    long long seq = 0;
    auto step = [&](const std::string& name, const std::string& result, bool ok,
                    const std::string& err) {
        j.writeLine("{" + join({kv("seq", seq++), kv("step", name),
                                "\"result\":" + result, kv("ok", ok),
                                kv("error", err)}) + "}");
    };

    const bool glfwInitOk = glfwInit() == GLFW_TRUE;
    step("glfw_init", "{" + kv("version", glfwGetVersionString()) + "}", glfwInitOk, "");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* win = glfwCreateWindow(640, 480, "terrain-exp", nullptr, nullptr);
    const char* desc = nullptr;
    const int glfwErr = glfwGetError(&desc);
    b.glfwWindowOk = win != nullptr;
    step("glfw_window_attempt",
         "{" + join({kv("window_created", b.glfwWindowOk),
                     kv("glfw_error_code", static_cast<long long>(glfwErr))}) + "}",
         true, b.glfwWindowOk ? "" : (desc ? desc : "unknown"));
    if (win) glfwDestroyWindow(win);

    const bool vkSupported = glfwVulkanSupported() == GLFW_TRUE;
    uint32_t reqCount = 0;
    glfwGetRequiredInstanceExtensions(&reqCount);
    step("glfw_vulkan_supported",
         "{" + join({kv("supported", vkSupported),
                     kv("required_instance_extensions", static_cast<long long>(reqCount))}) + "}",
         vkSupported, "");

    vk.loadGlobal();
    const auto instExts = vk.enumerateInstanceExtensions();
    std::string names;
    for (size_t i = 0; i < instExts.size(); ++i) {
        if (i) names += ",";
        names += instExts[i].extensionName;
    }
    step("vk_instance_extensions",
         "{" + join({kv("count", static_cast<long long>(instExts.size())),
                     kv("names", names)}) + "}",
         true, "");

    vk.createInstance({});
    vk.pickPhysicalDevice();
    b.deviceName = vk.deviceProps().deviceName;
    step("vk_instance_and_physical_device",
         "{" + join({kv("loader_version", static_cast<unsigned long long>(vk.loaderVersion())),
                     kv("device_name", b.deviceName),
                     kv("device_type", static_cast<long long>(vk.deviceProps().deviceType)),
                     kv("vendor_id", static_cast<unsigned long long>(vk.deviceProps().vendorID))}) + "}",
         true, "");

    const auto devExts = vk.enumerateDeviceExtensions();
    std::string dnames;
    for (size_t i = 0; i < devExts.size(); ++i) {
        if (i) dnames += ",";
        dnames += devExts[i].extensionName;
    }
    step("vk_device_extensions",
         "{" + join({kv("count", static_cast<long long>(devExts.size())),
                     kv("names", dnames)}) + "}",
         true, "");
    b.vulkanOk = true;
    j.close();
    return b;
}

int cmdAll(const std::string& evidenceDir) {
    fs::create_directories(evidenceDir);
    VkContext vk;
    const Bootstrap boot = bootstrap(vk, evidenceDir);
    if (!boot.vulkanOk) {
        std::cerr << "Vulkan bootstrap failed\n";
        return 1;
    }

    struct RunDef { terrain::Snapshot snap; std::string order; std::string name; };
    const std::vector<RunDef> runs = {
        {terrain::Snapshot::V1, "row", "run_v1_row"},
        {terrain::Snapshot::V1, "shuffled", "run_v1_shuffled"},
        {terrain::Snapshot::V2, "row", "run_v2_row"},
    };
    std::vector<RunResult> results;
    int replayMismatches = 0;
    for (const auto& def : runs) {
        vk.createDevice({});
        RunConfig cfg;
        cfg.snapshot = def.snap;
        cfg.order = def.order;
        cfg.runDir = evidenceDir + "/" + def.name;
        RunResult rr = runLoading(vk, cfg);
        vk.destroyDevice();
        results.push_back(rr);
        vk.createDevice({});
        replayMismatches += replayJournal(
            vk, cfg.runDir + "/journal.jsonl",
            evidenceDir + "/replay_" + def.name + ".json");
        vk.destroyDevice();
    }

    const bool orderIndependent = results[0].finalStateHash == results[1].finalStateHash;
    const bool snapshotContrast = results[0].finalStateHash != results[2].finalStateHash;
    const bool allCleanup = results[0].cleanupOk && results[1].cleanupOk && results[2].cleanupOk;

    vk.shutdown();
    glfwTerminate();

    std::ofstream sum(evidenceDir + "/summary.txt");
    sum << "chunked terrain loading experiment - summary\n"
        << "device: " << boot.deviceName << "\n"
        << "glfw_window_created: " << (boot.glfwWindowOk ? "yes" : "no (headless, recorded)") << "\n"
        << "unknown_extension_probe: " << kUnknownExtension << "\n\n";
    const char* names[3] = {"run_v1_row", "run_v1_shuffled", "run_v2_row"};
    for (int i = 0; i < 3; ++i) {
        sum << names[i]
            << " final_state_hash=" << results[i].finalStateHash
            << " loaded=" << results[i].loaded
            << " bytes=" << results[i].totalBytes
            << " exceptions=" << results[i].exceptions
            << " duplicates=" << results[i].duplicates
            << " cleanup=" << (results[i].cleanupOk ? "ok" : "FAILED") << "\n";
    }
    sum << "\norder_independence (v1 row == v1 shuffled): "
        << (orderIndependent ? "PASS" : "FAIL")
        << "\nsnapshot_contrast (v1 != v2): "
        << (snapshotContrast ? "PASS" : "FAIL")
        << "\nreplay_mismatches: " << replayMismatches
        << "\ntemp_state_cleaned: " << (allCleanup ? "yes" : "NO") << "\n";

    const bool pass = orderIndependent && snapshotContrast && replayMismatches == 0 && allCleanup;
    sum << "overall: " << (pass ? "PASS" : "FAIL") << "\n";
    std::cout << "evidence written to " << evidenceDir
              << "  overall=" << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 2;
}

} // namespace

int main(int argc, char** argv) {
    const std::string cmd = argc > 1 ? argv[1] : "all";
    try {
        if (cmd == "all") {
            std::string out = "artifacts/evidence";
            for (int i = 2; i + 1 < argc; ++i)
                if (std::string(argv[i]) == "--out") out = argv[i + 1];
            return cmdAll(out);
        }
        if (cmd == "replay") {
            std::string journalPath, out = "replay_report.json";
            for (int i = 2; i + 1 < argc; ++i) {
                const std::string a = argv[i];
                if (a == "--journal") journalPath = argv[i + 1];
                else if (a == "--out") out = argv[i + 1];
            }
            if (journalPath.empty()) {
                std::cerr << "usage: terrain_exp replay --journal J --out R\n";
                return 64;
            }
            VkContext vk;
            vk.loadGlobal();
            vk.createInstance({});
            vk.pickPhysicalDevice();
            vk.createDevice({});
            const int mism = replayJournal(vk, journalPath, out);
            vk.shutdown();
            std::cout << "replay mismatches: " << mism << "\n";
            return mism == 0 ? 0 : 2;
        }
        std::cerr << "usage: terrain_exp [all [--out DIR] | replay --journal J --out R]\n";
        return 64;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
