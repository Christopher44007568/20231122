#include "checkpoint.hpp"
#include "config.hpp"
#include "evidence.hpp"
#include "renderer.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

std::string resolvePath(const std::string& path)
{
    if (path.empty() || std::filesystem::path(path).is_absolute() ||
        std::filesystem::exists(path)) {
        return path;
    }
    const std::filesystem::path rooted =
        std::filesystem::path(FOG_ASSET_DIR) / path;
    return std::filesystem::exists(rooted) ? rooted.string() : path;
}

void printPlan(const VulkanFogRenderer& renderer)
{
    const MemoryPlan& plan = renderer.plan();
    std::cout << "device: " << renderer.deviceName() << " ("
              << renderer.deviceType() << ")\n"
              << "memory: " << (renderer.deviceMemoryBytes() / (1024ull * 1024ull))
              << " MiB, budget: "
              << (renderer.memoryBudgetBytes() / (1024ull * 1024ull))
              << " MiB\n"
              << "render plan: tier " << plan.tier << " (" << plan.label
              << "), " << plan.width << "x" << plan.height << ", "
              << plan.steps << " steps\n";
    for (const std::string& reason : plan.degradations) {
        std::cout << "degradation: " << reason << "\n";
    }
}

void fillRunEnvironment(EvidenceRun& run, const VulkanFogRenderer& renderer,
                        const FogConfig& config, const RunOptions& options,
                        const std::string& mode)
{
    run.mode = mode;
    run.timestampUtc = utcTimestamp();
    run.config = config;
    run.plan = renderer.plan();
    run.deviceName = renderer.deviceName();
    run.deviceType = renderer.deviceType();
    run.deviceMemoryBytes = renderer.deviceMemoryBytes();
    run.memoryBudgetBytes = renderer.memoryBudgetBytes();
    run.simulatedBudget = options.simulatedVramMb > 0;
    run.warnings = renderer.warnings();
}

bool framesAreFinite(const EvidenceFrame& frame)
{
    return frame.metrics.nanCount == 0 && frame.metrics.infCount == 0;
}

void finalizeStatus(EvidenceRun& run)
{
    const bool finite = framesAreFinite(run.beforeFrame) &&
                        framesAreFinite(run.afterFrame) &&
                        framesAreFinite(run.replayFrame) &&
                        framesAreFinite(run.continuationFrame);
    const bool missingFragmentExercised = run.afterFrame.gpuStats[1] > 0;
    const float boundaryDelta = std::max(
        std::abs(run.boundaryChange.probeDeltaLuminance),
        std::abs(run.boundaryChange.hotspotDeltaLuminance));
    const bool stableBoundary =
        boundaryDelta < 0.35f &&
        run.boundaryChange.meanAbsDifference < 0.08f &&
        run.boundaryChange.rmse < 0.15f;
    run.status = finite && missingFragmentExercised && stableBoundary &&
                         run.replayExact
        ? "pass"
        : "review";
}

EvidenceFrame makeEvidenceFrame(const FrameSnapshot& snapshot)
{
    EvidenceFrame frame;
    frame.metrics = snapshot.metrics;
    frame.gpuStats = snapshot.gpuStats;
    return frame;
}

void renderContinuation(VulkanFogRenderer& renderer, uint64_t firstFrame,
                        int count, EvidenceRun& run)
{
    FrameSnapshot last;
    for (int index = 0; index < count; ++index) {
        const uint64_t frame = firstFrame + static_cast<uint64_t>(index);
        renderer.renderFrame(frame, false);
        last = renderer.readbackSnapshot();
    }
    run.continuationImage = last.image;
    run.continuationFrame = makeEvidenceFrame(last);
    std::cout << "continuation frame " << last.frameIndex << " complete\n";
}

int runHeadless(VulkanFogRenderer& renderer, const FogConfig& config,
                const RunOptions& options, const std::string& evidenceDir)
{
    std::filesystem::create_directories(evidenceDir);
    const uint64_t boundary = static_cast<uint64_t>(config.warmupFrames);
    for (uint64_t frame = 0; frame < boundary; ++frame) {
        renderer.renderFrame(frame, false);
        if ((frame + 1) % 8 == 0 || frame + 1 == boundary) {
            std::cout << "warmup frame " << frame << " complete\n";
        }
    }

    const FrameSnapshot beforeSnapshot = renderer.readbackSnapshot();
    const std::string checkpointPath =
        (std::filesystem::path(evidenceDir) / "checkpoint.fogcp").string();
    const CheckpointData checkpoint =
        renderer.saveCheckpoint(checkpointPath, boundary);
    std::cout << "checkpoint saved: " << checkpointPath
              << " (" << checkpoint.fileBytes << " bytes, fnv1a64 "
              << std::hex << checkpoint.hash << std::dec << ")\n";

    const CheckpointData loaded = loadCheckpoint(checkpointPath);
    renderer.restoreCheckpoint(loaded);
    renderer.renderFrame(loaded.nextFrame, false);
    const FrameSnapshot afterSnapshot = renderer.readbackSnapshot();

    renderer.restoreCheckpoint(loaded);
    renderer.renderFrame(loaded.nextFrame, false);
    const FrameSnapshot replaySnapshot = renderer.readbackSnapshot();

    EvidenceRun run;
    fillRunEnvironment(run, renderer, config, options, "headless-resume");
    run.checkpoint = loaded;
    run.boundaryFrameBefore =
        loaded.nextFrame > 0 ? loaded.nextFrame - 1 : 0;
    run.boundaryFrameAfter = loaded.nextFrame;
    run.beforeImage = loaded.toImage();
    run.afterImage = afterSnapshot.image;
    run.replayImage = replaySnapshot.image;
    run.beforeFrame = makeEvidenceFrame(beforeSnapshot);
    run.afterFrame = makeEvidenceFrame(afterSnapshot);
    run.replayFrame = makeEvidenceFrame(replaySnapshot);
    run.boundaryChange =
        compareBoundary(run.beforeImage, run.afterImage, 0.5f, 0.48f);
    run.replayChange =
        compareBoundary(run.afterImage, run.replayImage, 0.5f, 0.48f);
    run.replayExact =
        afterSnapshot.metrics.hash == replaySnapshot.metrics.hash;
    const ImageMetrics serializedMetrics = measureImage(loaded.toImage());
    if (serializedMetrics.hash != beforeSnapshot.metrics.hash) {
        run.warnings.push_back(
            "checkpoint image hash differs from pre-save readback");
    }

    const int continuationCount = std::max(1, config.continuationFrames);
    renderContinuation(renderer, loaded.nextFrame + 1, continuationCount, run);
    finalizeStatus(run);
    writeEvidenceBundle(evidenceDir, run);
    return run.status == "pass" ? 0 : 2;
}

int runResumeOnly(VulkanFogRenderer& renderer, const FogConfig& config,
                  const RunOptions& options, const std::string& evidenceDir,
                  const std::string& checkpointPath)
{
    const CheckpointData checkpoint = loadCheckpoint(checkpointPath);
    renderer.restoreCheckpoint(checkpoint);
    renderer.renderFrame(checkpoint.nextFrame, false);
    const FrameSnapshot afterSnapshot = renderer.readbackSnapshot();
    renderer.restoreCheckpoint(checkpoint);
    renderer.renderFrame(checkpoint.nextFrame, false);
    const FrameSnapshot replaySnapshot = renderer.readbackSnapshot();

    EvidenceRun run;
    fillRunEnvironment(run, renderer, config, options, "resume-only");
    run.checkpoint = checkpoint;
    run.boundaryFrameBefore =
        checkpoint.nextFrame > 0 ? checkpoint.nextFrame - 1 : 0;
    run.boundaryFrameAfter = checkpoint.nextFrame;
    run.beforeImage = checkpoint.toImage();
    run.afterImage = afterSnapshot.image;
    run.replayImage = replaySnapshot.image;
    FrameSnapshot checkpointSnapshot;
    checkpointSnapshot.image = run.beforeImage;
    checkpointSnapshot.metrics = measureImage(run.beforeImage);
    checkpointSnapshot.frameIndex =
        checkpoint.nextFrame > 0 ? checkpoint.nextFrame - 1 : 0;
    run.beforeFrame = makeEvidenceFrame(checkpointSnapshot);
    run.afterFrame = makeEvidenceFrame(afterSnapshot);
    run.replayFrame = makeEvidenceFrame(replaySnapshot);
    run.boundaryChange =
        compareBoundary(run.beforeImage, run.afterImage, 0.5f, 0.48f);
    run.replayChange =
        compareBoundary(run.afterImage, run.replayImage, 0.5f, 0.48f);
    run.replayExact =
        afterSnapshot.metrics.hash == replaySnapshot.metrics.hash;
    const int continuationCount = std::max(1, config.continuationFrames);
    renderContinuation(renderer, checkpoint.nextFrame + 1, continuationCount,
                       run);
    finalizeStatus(run);
    writeEvidenceBundle(evidenceDir, run);
    return run.status == "pass" ? 0 : 2;
}

int runInteractive(VulkanFogRenderer& renderer, const FogConfig& config,
                   const RunOptions& options, const std::string& evidenceDir)
{
    std::filesystem::create_directories(evidenceDir);
    const std::string checkpointPath =
        (std::filesystem::path(evidenceDir) / "checkpoint.fogcp").string();
    uint64_t frame = 0;
    bool paused = false;
    bool previousPauseKey = false;
    CheckpointData checkpoint;
    EvidenceRun run;
    fillRunEnvironment(run, renderer, config, options, "interactive");
    const bool present = !options.disablePresentation;

    std::cout << "interactive: P pauses/resumes, Esc exits\n";
    while (!renderer.shouldClose()) {
        renderer.pollEvents();
        if (glfwGetKey(renderer.window(), GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            break;
        }
        const bool pauseKey =
            glfwGetKey(renderer.window(), GLFW_KEY_P) == GLFW_PRESS ||
            glfwGetKey(renderer.window(), GLFW_KEY_SPACE) == GLFW_PRESS;
        if (pauseKey && !previousPauseKey) {
            if (!paused) {
                const FrameSnapshot before = renderer.readbackSnapshot();
                checkpoint = renderer.saveCheckpoint(checkpointPath, frame);
                run.checkpoint = checkpoint;
                run.beforeImage = checkpoint.toImage();
                run.beforeFrame = makeEvidenceFrame(before);
                run.boundaryFrameBefore = frame > 0 ? frame - 1 : 0;
                paused = true;
                std::cout << "paused at boundary before frame " << frame
                          << "; checkpoint " << checkpointPath << "\n";
            } else {
                const CheckpointData loaded = loadCheckpoint(checkpointPath);
                renderer.restoreCheckpoint(loaded);
                renderer.renderFrame(loaded.nextFrame, present);
                const FrameSnapshot after = renderer.readbackSnapshot();
                renderer.restoreCheckpoint(loaded);
                renderer.renderFrame(loaded.nextFrame, present);
                const FrameSnapshot replay = renderer.readbackSnapshot();

                run.afterImage = after.image;
                run.replayImage = replay.image;
                run.afterFrame = makeEvidenceFrame(after);
                run.replayFrame = makeEvidenceFrame(replay);
                run.boundaryFrameAfter = loaded.nextFrame;
                run.boundaryChange = compareBoundary(
                    run.beforeImage, run.afterImage, 0.5f, 0.48f);
                run.replayChange = compareBoundary(
                    run.afterImage, run.replayImage, 0.5f, 0.48f);
                run.replayExact = after.metrics.hash == replay.metrics.hash;
                const int continuationCount =
                    std::max(1, config.continuationFrames);
                renderContinuation(renderer, loaded.nextFrame + 1,
                                   continuationCount, run);
                finalizeStatus(run);
                writeEvidenceBundle(evidenceDir, run);
                frame = loaded.nextFrame + continuationCount + 1;
                paused = false;
                std::cout << "resumed; status " << run.status << "\n";
            }
        }
        previousPauseKey = pauseKey;
        if (!paused) {
            renderer.renderFrame(frame, present);
            ++frame;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        RunOptions options = parseRunOptions(argc, argv);
        if (options.configPath.empty()) {
            options.configPath =
                "configs/missing_fragment_stress.ini";
        }
        const std::string configPath = resolvePath(options.configPath);
        FogConfig config = loadFogConfig(configPath);
        if (options.warmupFrames >= 0) {
            config.warmupFrames = options.warmupFrames;
        }
        if (options.continuationFrames >= 0) {
            config.continuationFrames = options.continuationFrames;
        }
        sanitizeFogConfig(config);
        options.evidenceDir = resolvePath(options.evidenceDir);
        if (std::filesystem::path(options.evidenceDir).is_relative()) {
            options.evidenceDir =
                (std::filesystem::current_path() / options.evidenceDir).string();
        }

        std::cout << "config: " << configPath << "\n"
                  << "scenario: " << config.scenario << "\n";
        VulkanFogRenderer renderer(config, options);
        renderer.initialize();
        printPlan(renderer);

        if (!options.resumePath.empty()) {
            return runResumeOnly(renderer, config, options, options.evidenceDir,
                                 resolvePath(options.resumePath));
        }
        if (options.headless) {
            return runHeadless(renderer, config, options, options.evidenceDir);
        }
        return runInteractive(renderer, config, options, options.evidenceDir);
    } catch (const std::exception& error) {
        std::cerr << "fog_stability: " << error.what() << "\n";
        return 1;
    }
}
