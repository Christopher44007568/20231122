#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "artifact.h"
#include "ordered_json.h"
#include "pipeline.h"
#include "renderer.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace ph {
namespace {

struct Options {
    AppConfig config;
    bool help = false;
};

void printUsage() {
    std::cout
        << "pixel_history - pausable pixel-history sampling (C++17 / Vulkan / GLFW)\n\n"
        << "Usage:\n"
        << "  pixel_history [options]\n\n"
        << "Modes:\n"
        << "  --mode both         continuous run, paused checkpoint, resume and compare (default)\n"
        << "  --mode continuous   uninterrupted reference run\n"
        << "  --mode paused       stop after --pause-after steps and write checkpoint.json\n"
        << "  --mode resume       load --in checkpoint and continue to completion\n"
        << "  --mode verify       independently verify --in JSON evidence\n"
        << "  --mode interactive  GLFW window with a live Vulkan offscreen preview\n\n"
        << "Options:\n"
        << "  --out DIR                output directory (default artifacts/demo)\n"
        << "  --in FILE                checkpoint for resume, or artifact for verify\n"
        << "  --frames N               number of sampling steps (default 12, min 2 for pause modes)\n"
        << "  --pause-after N          checkpoint after N steps (default 5)\n"
        << "  --memory-budget-kb N     working-set budget (default 16384)\n"
        << "  --max-quality NAME       high, balanced, low, minimal or emergency\n"
        << "  --json                   print a machine-readable summary\n"
        << "  --help                   show this message\n\n"
        << "Controls in interactive mode: Space pause/resume, R restart, Esc quit.\n";
}

bool parseInteger(const std::string& text, int64_t& value) {
    try {
        size_t consumed = 0;
        const long long parsed = std::stoll(text, &consumed, 10);
        if (consumed != text.size()) {
            return false;
        }
        value = static_cast<int64_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseOptions(int argc, char** argv, Options& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&](const char* option) -> std::string {
            if (i + 1 >= argc) {
                error = std::string("missing value for ") + option;
                return {};
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--json") {
            options.config.json_stdout = true;
        } else if (arg == "--mode") {
            options.config.mode = value("--mode");
        } else if (arg == "--out") {
            options.config.output_dir = value("--out");
        } else if (arg == "--in") {
            options.config.input_path = value("--in");
        } else if (arg == "--max-quality") {
            options.config.max_quality = value("--max-quality");
        } else if (arg == "--frames") {
            int64_t number = 0;
            const std::string text = value("--frames");
            if (!parseInteger(text, number) || number < 1 || number > 256) {
                error = "--frames must be 1..256";
                return false;
            }
            options.config.clock.frame_count = static_cast<int>(number);
        } else if (arg == "--pause-after") {
            int64_t number = 0;
            const std::string text = value("--pause-after");
            if (!parseInteger(text, number) || number < 1 || number > 255) {
                error = "--pause-after must be 1..255";
                return false;
            }
            options.config.pause_after = static_cast<int>(number);
        } else if (arg == "--memory-budget-kb") {
            int64_t number = 0;
            const std::string text = value("--memory-budget-kb");
            if (!parseInteger(text, number) || number < 1) {
                error = "--memory-budget-kb must be positive";
                return false;
            }
            options.config.memory_budget_bytes = number * 1024;
        } else {
            error = "unknown option: " + arg;
            return false;
        }
        if (!error.empty()) {
            return false;
        }
    }

    const std::string& mode = options.config.mode;
    if (mode != "both" && mode != "continuous" && mode != "paused" &&
        mode != "resume" && mode != "verify" && mode != "interactive") {
        error = "--mode must be both, continuous, paused, resume, verify or interactive";
        return false;
    }
    if (options.config.max_quality != "high" && options.config.max_quality != "balanced" &&
        options.config.max_quality != "low" && options.config.max_quality != "minimal" &&
        options.config.max_quality != "emergency") {
        error = "--max-quality must be high, balanced, low, minimal or emergency";
        return false;
    }
    if ((mode == "verify" || mode == "resume") && options.config.input_path.empty()) {
        error = "--in is required for " + mode;
        return false;
    }
    if (mode == "both" || mode == "paused") {
        if (options.config.clock.frame_count < 2) {
            error = "pause modes require at least 2 frames";
            return false;
        }
        if (options.config.pause_after >= options.config.clock.frame_count) {
            options.config.pause_after = options.config.clock.frame_count - 1;
        }
    }
    return true;
}

std::filesystem::path outputPath(const AppConfig& config, const std::string& name) {
    return std::filesystem::path(config.output_dir) / name;
}

bool writeReview(const std::string& artifact, const std::filesystem::path& report_path,
                 bool print_report, bool& ok, std::string& error) {
    std::string report;
    ok = verifyArtifact(artifact, report, error);
    if (report.empty()) {
        return false;
    }
    if (report_path.has_parent_path()) {
        std::filesystem::create_directories(report_path.parent_path());
    }
    std::ofstream stream(report_path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "cannot write review report: " + report_path.string();
        return false;
    }
    stream.write(report.data(), static_cast<std::streamsize>(report.size()));
    if (!stream) {
        error = "cannot write review report: " + report_path.string();
        return false;
    }
    if (print_report) {
        std::cout << report;
    }
    return true;
}

bool openRenderer(const AppConfig& config, ResourcePlan& plan, GLFWwindow* window,
                  std::unique_ptr<VulkanRenderer>& renderer, std::string& error) {
    while (true) {
        const std::string attempt =
            plan.effective_quality + ":" + std::to_string(plan.width) + "x" +
            std::to_string(plan.height);
        plan.allocation_attempts.push_back(attempt + ":requested");
        auto candidate = std::make_unique<VulkanRenderer>();
        std::string init_error;
        if (candidate->initialize(plan.width, plan.height, window, init_error)) {
            plan.allocation_attempts.push_back(attempt + ":success");
            renderer = std::move(candidate);
            return true;
        }
        plan.allocation_attempts.push_back(attempt + ":failed: " + init_error);
        candidate.reset();
        if (!degradeResourcePlan(plan, config, "vulkan_initialization_failed")) {
            error = "Vulkan initialization failed at every quality profile: " + init_error;
            return false;
        }
    }
}

bool writeFailure(const AppConfig& config, const ResourcePlan& plan, std::string& path,
                  std::string& error) {
    path = outputPath(config, "failure.json").string();
    return writeBlockedArtifact(path, config, plan, error);
}

bool isVulkanRuntimeFailure(const std::string& error) {
    return error.find("Vulkan") != std::string::npos ||
           error.find("vkQueue") != std::string::npos ||
           error.find("VkResult") != std::string::npos;
}

bool executeContinuous(const AppConfig& config, ResourcePlan& plan, RunResult& result,
                       std::string& error) {
    for (int attempt = 0; attempt < 6; ++attempt) {
        RunResult candidate = makeRunResult(config, plan);
        std::unique_ptr<VulkanRenderer> renderer;
        if (!openRenderer(config, plan, nullptr, renderer, error)) {
            return false;
        }
        candidate.plan = plan;
        candidate.runtime_device = renderer->deviceName();
        candidate.runtime_api_version = renderer->apiVersion();
        std::string processing_error;
        if (processFrames(config, candidate, *renderer, 0, config.clock.frame_count,
                          processing_error)) {
            candidate.status = "complete";
            candidate.next_step = config.clock.frame_count;
            candidate.pre_summary = summarize(candidate, 0, -1);
            candidate.post_summary = summarize(candidate, 0, -1);
            result = std::move(candidate);
            return true;
        }
        if (!isVulkanRuntimeFailure(processing_error) ||
            !degradeResourcePlan(plan, config, "vulkan_runtime_failure")) {
            error = processing_error;
            return false;
        }
        candidate.notes.push_back("retry_after_vulkan_failure");
    }
    error = "continuous run exhausted all Vulkan fallback attempts";
    return false;
}

bool executePrefix(const AppConfig& config, ResourcePlan& plan, RunResult& result,
                   std::string& error) {
    for (int attempt = 0; attempt < 6; ++attempt) {
        RunResult candidate = makeRunResult(config, plan);
        std::unique_ptr<VulkanRenderer> renderer;
        if (!openRenderer(config, plan, nullptr, renderer, error)) {
            return false;
        }
        candidate.plan = plan;
        candidate.runtime_device = renderer->deviceName();
        candidate.runtime_api_version = renderer->apiVersion();
        std::string processing_error;
        if (processFrames(config, candidate, *renderer, 0, config.pause_after,
                          processing_error)) {
            candidate.status = "paused";
            candidate.paused = true;
            candidate.checkpoint_path = "checkpoint.json";
            candidate.pre_summary = summarize(candidate, 0, -1);
            candidate.post_summary =
                summarize(candidate, config.pause_after, -1);
            result = std::move(candidate);
            return true;
        }
        if (!isVulkanRuntimeFailure(processing_error) ||
            !degradeResourcePlan(plan, config, "vulkan_runtime_failure")) {
            error = processing_error;
            return false;
        }
        candidate.notes.push_back("retry_after_vulkan_failure");
    }
    error = "paused prefix exhausted all Vulkan fallback attempts";
    return false;
}

bool executeResume(const AppConfig& config, LoadedCheckpoint loaded, const std::string& mode_name,
                   RunResult& result, std::string& error) {
    result = std::move(loaded.prefix);
    result.mode = mode_name;
    result.status = "processing";
    result.paused = false;
    result.checkpoint_path = std::filesystem::path(config.input_path).filename().string();
    result.resume.loaded = true;
    result.resume.artifact_digest_valid = loaded.digest_valid;
    result.resume.checkpoint_roundtrip_byte_stable = loaded.roundtrip_byte_stable;
    result.resume.checkpoint_field_order_preserved = loaded.field_order_preserved;
    result.resume.checkpoint_digest = loaded.artifact_digest;
    result.resume_start_step = loaded.next_step;
    result.pre_summary = summarize(result, 0, loaded.next_step);
    result.post_summary = summarize(result, loaded.next_step, -1);

    const std::string checkpoint_quality = result.plan.effective_quality;
    ResourcePlan plan = result.plan;
    std::unique_ptr<VulkanRenderer> renderer;
    if (!openRenderer(config, plan, nullptr, renderer, error)) {
        return false;
    }
    result.plan = plan;
    if (plan.effective_quality != checkpoint_quality) {
        result.notes.push_back("quality_changed_on_resume:" + checkpoint_quality + "->" +
                               plan.effective_quality);
    }
    result.runtime_device = renderer->deviceName();
    result.runtime_api_version = renderer->apiVersion();
    if (!verifyResumeFrame(config, plan, *renderer, result, result.resume, error)) {
        return false;
    }
    if (!processFrames(config, result, *renderer, loaded.next_step, result.frame_count, error)) {
        return false;
    }
    result.status = result.resume.match ? "complete" : "resume_mismatch";
    result.pre_summary = summarize(result, 0, loaded.next_step);
    result.post_summary = summarize(result, loaded.next_step, -1);
    if (!result.resume.match) {
        result.notes.push_back("resume_verification_mismatch");
    }
    return true;
}

void glfwErrorCallback(int code, const char* description) {
    std::cerr << "GLFW error " << code << ": "
              << (description == nullptr ? "unknown" : description) << "\n";
}

int runInteractive(const AppConfig& config) {
    if (!glfwInit()) {
        std::cerr << "error: GLFW initialization failed\n";
        return 2;
    }
    if (!glfwVulkanSupported()) {
        glfwTerminate();
        std::cerr << "error: GLFW could not find a Vulkan loader\n";
        return 2;
    }
    glfwSetErrorCallback(glfwErrorCallback);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window =
        glfwCreateWindow(960, 600, "Pixel History Sampler - Vulkan", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        std::cerr << "error: GLFW window creation failed\n";
        return 2;
    }

    ResourcePlan plan = chooseResourcePlan(config);
    if (plan.blocked) {
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cerr << "error: " << plan.reason << "\n";
        return 3;
    }
    std::unique_ptr<VulkanRenderer> renderer;
    std::string error;
    if (!openRenderer(config, plan, window, renderer, error)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cerr << "error: " << error << "\n";
        return 3;
    }

    std::cout << "interactive: Space=pause/resume, R=restart, Esc=quit; quality="
              << plan.effective_quality << " " << plan.width << "x" << plan.height
              << ", device=" << renderer->deviceName() << "\n";

    int step = 0;
    bool paused = false;
    bool dirty = true;
    bool space_previous = false;
    auto next_tick = std::chrono::steady_clock::now();
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        const bool space = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (space && !space_previous) {
            paused = !paused;
            dirty = true;
        }
        space_previous = space;
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            step = 0;
            next_tick = std::chrono::steady_clock::now();
            dirty = true;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!paused && now >= next_tick) {
            step = (step + 1) % config.clock.frame_count;
            next_tick = now + std::chrono::milliseconds(300);
            dirty = true;
        }
        if (dirty) {
            const ClockStep clocks = clockStep(config.clock, step);
            const int device = step % 2;
            const int64_t scene_tick =
                device == 0 ? clocks.device_a_scene_us : clocks.device_b_scene_us;
            std::vector<uint8_t> pixels;
            if (!renderer->renderFrame(scene_tick, config.clock, static_cast<uint32_t>(device),
                                       pixels, error) ||
                !renderer->present(error)) {
                std::cerr << "error: " << error << "\n";
                break;
            }
            const std::string title =
                "Pixel History Sampler - step " + std::to_string(step) + "/" +
                std::to_string(config.clock.frame_count) + " - " +
                (device == 0 ? "device A" : "device B") +
                (paused ? " [paused]" : " [running]") + " - " + plan.effective_quality;
            glfwSetWindowTitle(window, title.c_str());
            dirty = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    renderer.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

int printRunAndStatus(const RunResult& result, const AppConfig& config,
                      const std::string& artifact_path, std::string& error) {
    bool review_ok = false;
    const std::filesystem::path review_path =
        outputPath(config, std::filesystem::path(artifact_path).stem().string() + ".review.json");
    if (!writeReview(artifact_path, review_path, false, review_ok, error)) {
        return 2;
    }
    if (config.json_stdout) {
        std::cout << summarizeRunJson(result);
    } else {
        std::cout << "artifact: " << artifact_path << "\n"
                  << "review:   " << review_path.string() << "\n"
                  << "status:   " << result.status << "\n"
                  << "quality:  " << result.plan.effective_quality << " "
                  << result.plan.width << "x" << result.plan.height << "\n"
                  << "digest:   " << streamDigest(result) << "\n";
    }
    const bool resume_ok = !result.resume.loaded || result.resume.match;
    return review_ok && resume_ok ? 0 : 1;
}

int runContinuousMode(const AppConfig& config) {
    ResourcePlan plan = chooseResourcePlan(config);
    if (plan.blocked) {
        std::string path;
        std::string error;
        if (!writeFailure(config, plan, path, error)) {
            std::cerr << "error: " << error << "\n";
            return 2;
        }
        std::cerr << "error: " << plan.reason << "\nfailure evidence: " << path << "\n";
        return 3;
    }
    RunResult result;
    std::string error;
    if (!executeContinuous(config, plan, result, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    const std::string path = outputPath(config, "run-continuous.json").string();
    if (!writeRunArtifact(path, result, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    return printRunAndStatus(result, config, path, error);
}

int runPausedMode(const AppConfig& config) {
    ResourcePlan plan = chooseResourcePlan(config);
    if (plan.blocked) {
        std::string path;
        std::string error;
        if (!writeFailure(config, plan, path, error)) {
            std::cerr << "error: " << error << "\n";
            return 2;
        }
        std::cerr << "error: " << plan.reason << "\nfailure evidence: " << path << "\n";
        return 3;
    }
    RunResult prefix;
    std::string error;
    if (!executePrefix(config, plan, prefix, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    const std::string path = outputPath(config, "checkpoint.json").string();
    prefix.checkpoint_path = "checkpoint.json";
    if (!writeCheckpointArtifact(path, prefix, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    if (config.json_stdout) {
        std::cout << summarizeCheckpointJson(prefix);
    } else {
        std::cout << "checkpoint: " << path << "\n"
                  << "status:     paused after " << prefix.next_step << "/"
                  << prefix.frame_count << " steps\n"
                  << "quality:    " << prefix.plan.effective_quality << " "
                  << prefix.plan.width << "x" << prefix.plan.height << "\n"
                  << "digest:     " << streamDigest(prefix) << "\n"
                  << "resume:     pixel_history --mode resume --in " << path
                  << " --out " << config.output_dir << "\n";
    }
    return 0;
}

int runResumeMode(const AppConfig& config) {
    LoadedCheckpoint loaded;
    std::string error;
    if (!loadCheckpointArtifact(config.input_path, loaded, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    const std::filesystem::path target_checkpoint =
        outputPath(config, "checkpoint.json");
    if (std::filesystem::absolute(target_checkpoint).lexically_normal() !=
        std::filesystem::absolute(config.input_path).lexically_normal()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(target_checkpoint.parent_path());
        std::filesystem::copy_file(config.input_path, target_checkpoint,
                                   std::filesystem::copy_options::overwrite_existing,
                                   filesystem_error);
        if (filesystem_error) {
            std::cerr << "error: cannot preserve checkpoint beside the resumed artifact: "
                      << filesystem_error.message() << "\n";
            return 2;
        }
    }
    RunResult result;
    if (!executeResume(config, std::move(loaded), "resume", result, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    if (result.status == "resume_mismatch") {
        error = "resume pixel comparison failed";
    }
    const std::string path = outputPath(config, "run-paused.json").string();
    if (!writeRunArtifact(path, result, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    const int status = printRunAndStatus(result, config, path, error);
    return result.status == "resume_mismatch" ? 1 : status;
}

int runBothMode(const AppConfig& config) {
    ResourcePlan plan = chooseResourcePlan(config);
    std::string error;
    if (plan.blocked) {
        std::string path;
        if (!writeFailure(config, plan, path, error)) {
            std::cerr << "error: " << error << "\n";
            return 2;
        }
        std::cerr << "error: " << plan.reason << "\nfailure evidence: " << path << "\n";
        return 3;
    }

    RunResult continuous;
    if (!executeContinuous(config, plan, continuous, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    const std::string continuous_path =
        outputPath(config, "run-continuous.json").string();
    if (!writeRunArtifact(continuous_path, continuous, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }

    ResourcePlan prefix_plan = plan;
    RunResult prefix;
    if (!executePrefix(config, prefix_plan, prefix, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    const std::string checkpoint_path = outputPath(config, "checkpoint.json").string();
    prefix.checkpoint_path = "checkpoint.json";
    if (!writeCheckpointArtifact(checkpoint_path, prefix, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }

    LoadedCheckpoint loaded;
    if (!loadCheckpointArtifact(checkpoint_path, loaded, error)) {
        std::cerr << "error: reloaded checkpoint failed validation: " << error << "\n";
        return 2;
    }
    AppConfig resume_config = config;
    resume_config.input_path = checkpoint_path;
    RunResult paused;
    if (!executeResume(resume_config, std::move(loaded), "pause-resume", paused, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }

    paused.comparison.evaluated = true;
    paused.comparison.reference_path = "run-continuous.json";
    paused.comparison.reference_digest = streamDigest(continuous);
    paused.comparison.current_digest = streamDigest(paused);
    paused.comparison.match =
        paused.comparison.reference_digest == paused.comparison.current_digest;
    if (!paused.comparison.match) {
        paused.status = "comparison_mismatch";
        paused.notes.push_back("paused_resume_stream_differs_from_continuous_run");
    }

    const std::string paused_path = outputPath(config, "run-paused.json").string();
    if (!writeRunArtifact(paused_path, paused, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }

    bool continuous_review_ok = false;
    bool paused_review_ok = false;
    const auto continuous_review =
        outputPath(config, "run-continuous.review.json");
    const auto paused_review = outputPath(config, "run-paused.review.json");
    if (!writeReview(continuous_path, continuous_review, false, continuous_review_ok, error) ||
        !writeReview(paused_path, paused_review, false, paused_review_ok, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }

    if (config.json_stdout) {
        Json summary = Json::object();
        summary.set("schema", Json("pixel-history.both-summary"));
        summary.set("continuous_artifact", Json(continuous_path));
        summary.set("paused_artifact", Json(paused_path));
        summary.set("checkpoint", Json(checkpoint_path));
        summary.set("continuous_digest", Json(streamDigest(continuous)));
        summary.set("paused_digest", Json(streamDigest(paused)));
        summary.set("resume_match", Json(paused.resume.match));
        summary.set("continuous_match", Json(paused.comparison.match));
        summary.set("quality", Json(paused.plan.effective_quality));
        summary.set("resolution", Json(std::to_string(paused.plan.width) + "x" +
                                       std::to_string(paused.plan.height)));
        summary.set("degradations", Json(static_cast<int>(paused.plan.degradations.size())));
        summary.set("review_continuous_ok", Json(continuous_review_ok));
        summary.set("review_paused_ok", Json(paused_review_ok));
        std::cout << summary.dumpPretty(2);
    } else {
        std::cout << "continuous: " << continuous_path << "\n"
                  << "checkpoint: " << checkpoint_path << "\n"
                  << "paused:     " << paused_path << "\n"
                  << "review:     " << paused_review.string() << "\n"
                  << "quality:    " << paused.plan.effective_quality << " "
                  << paused.plan.width << "x" << paused.plan.height << "\n"
                  << "degraded:   " << paused.plan.degradations.size() << " step(s)\n"
                  << "resume:     " << (paused.resume.match ? "match" : "MISMATCH") << "\n"
                  << "continuous: " << (paused.comparison.match ? "match" : "MISMATCH")
                  << "\n"
                  << "digest:     " << streamDigest(paused) << "\n";
    }
    const bool ok = paused.resume.match && paused.comparison.match &&
                    continuous_review_ok && paused_review_ok;
    return ok ? 0 : 1;
}

int runVerifyMode(const AppConfig& config) {
    std::string report;
    std::string error;
    const bool ok = verifyArtifact(config.input_path, report, error);
    if (report.empty()) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    if (!config.output_dir.empty()) {
        const auto report_path = outputPath(config, "verify-report.json");
        if (report_path.has_parent_path()) {
            std::filesystem::create_directories(report_path.parent_path());
        }
        std::ofstream stream(report_path, std::ios::binary | std::ios::trunc);
        if (stream) {
            stream.write(report.data(), static_cast<std::streamsize>(report.size()));
        }
    }
    std::cout << report;
    return ok ? 0 : 1;
}

}  // namespace
}  // namespace ph

int main(int argc, char** argv) {
    using namespace ph;
    Options options;
    std::string error;
    if (!parseOptions(argc, argv, options, error)) {
        std::cerr << "error: " << error << "\n\n";
        printUsage();
        return 2;
    }
    if (options.help) {
        printUsage();
        return 0;
    }
    const AppConfig& config = options.config;
    if (config.mode == "interactive") {
        return runInteractive(config);
    }
    if (config.mode == "verify") {
        return runVerifyMode(config);
    }
    if (config.mode == "continuous") {
        return runContinuousMode(config);
    }
    if (config.mode == "paused") {
        return runPausedMode(config);
    }
    if (config.mode == "resume") {
        return runResumeMode(config);
    }
    return runBothMode(config);
}
