#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "artifact.h"
#include "clock.h"
#include "model.h"
#include "pipeline.h"
#include "preview.h"
#include "renderer.h"
#include "sha256.h"
#include "util.h"

namespace {

struct CliOptions {
    std::string command = "run";
    Config config = defaultConfig();
    std::string argument;
    std::string checkpoint;
    bool pauseExplicit = false;
    bool outputExplicit = false;
    bool help = false;
    bool preview = false;
};

void printHelp() {
    std::cout
        << "bokeh-pipeline " << BOKEH_VERSION << " (C++17 + Vulkan + GLFW)\n\n"
        << "Usage:\n"
        << "  bokeh-pipeline [run] [options]\n"
        << "  bokeh-pipeline verify ARTIFACT_DIR\n"
        << "  bokeh-pipeline inspect ARTIFACT_DIR\n"
        << "  bokeh-pipeline preview PPM_FILE\n"
        << "  bokeh-pipeline resume --checkpoint FILE --out DIR\n\n"
        << "Run options:\n"
        << "  --out DIR             output directory (default artifacts/demo)\n"
        << "  --width N             image width, 16..4096 (default 320)\n"
        << "  --height N            image height, 16..4096 (default 200)\n"
        << "  --frames N            sequences, 2..256 (default 12)\n"
        << "  --pause-after N       pause after N observation tasks\n"
        << "  --memory-mb N         normal Vulkan transient budget\n"
        << "  --backend MODE        auto, vulkan, or cpu\n"
        << "  --seed N              deterministic scene seed\n"
        << "  --preview             open the resumed final frame after verification\n"
        << "  --checkpoint FILE     checkpoint for the resume subcommand\n";
}

uint64_t parseUint(const std::string& value, const std::string& option) {
    size_t consumed = 0;
    try {
        unsigned long long parsed = std::stoull(value, &consumed, 10);
        if (consumed != value.size()) throw std::invalid_argument("trailing");
        return static_cast<uint64_t>(parsed);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid value for " + option + ": " + value);
    }
}

CliOptions parseArguments(int argc, char** argv) {
    CliOptions options;
    int index = 1;
    if (index < argc && std::string(argv[index]) == "verify") {
        options.command = "verify";
        if (index + 1 < argc) options.argument = argv[++index];
        ++index;
    } else if (index < argc && std::string(argv[index]) == "inspect") {
        options.command = "inspect";
        if (index + 1 < argc) options.argument = argv[++index];
        ++index;
    } else if (index < argc && std::string(argv[index]) == "preview") {
        options.command = "preview";
        if (index + 1 < argc) options.argument = argv[++index];
        ++index;
    } else if (index < argc && std::string(argv[index]) == "resume") {
        options.command = "resume";
        ++index;
    } else if (index < argc && std::string(argv[index]) == "run") {
        ++index;
    }

    for (; index < argc; ++index) {
        std::string arg = argv[index];
        auto value = [&](const std::string& option) -> std::string {
            if (index + 1 >= argc) throw std::runtime_error("missing value for " + option);
            return argv[++index];
        };
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--out") {
            options.config.outputDirectory = value(arg);
            options.outputExplicit = true;
        } else if (arg == "--width") {
            options.config.width = static_cast<int>(parseUint(value(arg), arg));
        } else if (arg == "--height") {
            options.config.height = static_cast<int>(parseUint(value(arg), arg));
        } else if (arg == "--frames") {
            options.config.frames = static_cast<int>(parseUint(value(arg), arg));
        } else if (arg == "--pause-after") {
            options.config.pauseAfterTasks = static_cast<int>(parseUint(value(arg), arg));
            options.pauseExplicit = true;
        } else if (arg == "--memory-mb") {
            options.config.memoryBudgetBytes = parseUint(value(arg), arg) * 1024ULL * 1024ULL;
        } else if (arg == "--backend") {
            options.config.backend = value(arg);
        } else if (arg == "--seed") {
            options.config.seed = static_cast<uint32_t>(parseUint(value(arg), arg));
        } else if (arg == "--preview") {
            options.preview = true;
        } else if (arg == "--checkpoint") {
            options.checkpoint = value(arg);
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }
    return options;
}

std::string commandLine(int argc, char** argv) {
    std::ostringstream out;
    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.find_first_of(" \t\"'") != std::string::npos) {
            out << '"' << arg << '"';
        } else {
            out << arg;
        }
        if (i + 1 < argc) out << ' ';
    }
    return out.str();
}

void validateConfig(const Config& config, bool pauseExplicit) {
    if (config.width < 16 || config.width > 4096 || config.height < 16 || config.height > 4096) {
        throw std::runtime_error("width and height must be in 16..4096");
    }
    if (config.frames < 2 || config.frames > 256) {
        throw std::runtime_error("frames must be in 2..256");
    }
    if (config.backend != "auto" && config.backend != "vulkan" && config.backend != "cpu") {
        throw std::runtime_error("backend must be auto, vulkan, or cpu");
    }
    int totalTasks = config.frames * 2;
    if (pauseExplicit &&
        (config.pauseAfterTasks < 1 || config.pauseAfterTasks >= totalTasks)) {
        throw std::runtime_error("pause-after must be in 1.." + std::to_string(totalTasks - 1));
    }
}

void prepareRoot(const std::string& root) {
    if (root.empty() || root == "/" || std::filesystem::absolute(root) ==
                                        std::filesystem::current_path()) {
        throw std::runtime_error("refusing to clean unsafe output directory: " + root);
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (error) throw std::runtime_error("cannot clean output directory: " + error.message());
    ensureDirectory(root);
}

void copyFinalFrame(const RunResult& result, const std::string& directory) {
    std::string last = joinPath(directory, result.frames.back().file);
    copyFile(last, joinPath(directory, "final.ppm"));
}

void writeRunManifest(const std::string& directory,
                      const RunResult& result,
                      const ClockModel& clock,
                      const std::string& mode,
                      bool resumed,
                      const std::string& checkpointDisplay,
                      const std::string& checkpointSha,
                      int resumedFromTask) {
    Json manifest = buildManifest(result, clock, mode, resumed, checkpointDisplay,
                                  checkpointSha, resumedFromTask);
    writeJsonFile(joinPath(directory, "manifest.json"), manifest);
}

bool runResume(const CliOptions& options) {
    if (options.checkpoint.empty()) {
        throw std::runtime_error("resume requires --checkpoint FILE");
    }
    if (!options.outputExplicit) {
        throw std::runtime_error("resume requires --out DIR");
    }
    CheckpointData checkpoint = loadCheckpoint(options.checkpoint);
    ClockModel clock = buildClockModel(checkpoint.config);
    if (dumpJson(clockModelToJson(clock), false) !=
        dumpJson(*checkpoint.raw.get("device_clocks"), false)) {
        throw std::runtime_error("checkpoint device clocks do not match the rebuilt model");
    }
    int totalTasks = static_cast<int>(clock.observations.size());
    if (checkpoint.nextTaskIndex >= totalTasks) {
        throw std::runtime_error("checkpoint has no remaining tasks");
    }
    std::string root = options.config.outputDirectory;
    prepareRoot(root);
    RunOptions resumeOptions;
    resumeOptions.directory = root;
    resumeOptions.mode = "resumed-external";
    resumeOptions.startTask = checkpoint.nextTaskIndex;
    resumeOptions.stopTask = totalTasks;
    resumeOptions.resume = true;
    resumeOptions.checkpointPath = std::filesystem::absolute(options.checkpoint).string();
    resumeOptions.checkpointSha256 = checkpoint.sha256;
    resumeOptions.resumedFromTask = checkpoint.nextTaskIndex;
    resumeOptions.initialEvents = checkpoint.events;
    resumeOptions.fixedPlan = &checkpoint.plan;
    resumeOptions.seededFrames = checkpoint.frames;
    resumeOptions.seedFrameDirectory =
        std::filesystem::path(options.checkpoint).parent_path().string();
    RunResult result = executeRun(checkpoint.config, clock, resumeOptions);
    copyFinalFrame(result, root);
    writeRunManifest(root, result, clock, "resumed-external", true,
                     resumeOptions.checkpointPath, checkpoint.sha256,
                     checkpoint.nextTaskIndex);
    std::cout << "resumed_from_task: " << checkpoint.nextTaskIndex << '\n'
              << "completed_tasks: " << result.frames.size() << '\n'
              << "final_sha256: " << result.frames.back().sha256 << '\n'
              << "artifact_root: " << root << '\n';
    return true;
}

bool runSuite(const CliOptions& options, const std::string& invokedCommand) {
    Config config = options.config;
    int totalTasks = config.frames * 2;
    if (!options.pauseExplicit && config.pauseAfterTasks >= totalTasks) {
        config.pauseAfterTasks = std::max(1, totalTasks / 3);
    }
    validateConfig(config, true);
    std::string root = config.outputDirectory;
    prepareRoot(root);
    ClockModel clock = buildClockModel(config);

    RunOptions baselineOptions;
    baselineOptions.directory = joinPath(root, "baseline");
    baselineOptions.mode = "baseline";
    baselineOptions.startTask = 0;
    baselineOptions.stopTask = totalTasks;
    RunResult baseline = executeRun(config, clock, baselineOptions);
    copyFinalFrame(baseline, baselineOptions.directory);
    writeRunManifest(baselineOptions.directory, baseline, clock, "baseline",
                     false, "", "", 0);

    RunOptions pausedOptions;
    pausedOptions.directory = joinPath(root, "paused");
    pausedOptions.mode = "paused";
    pausedOptions.startTask = 0;
    pausedOptions.stopTask = config.pauseAfterTasks;
    pausedOptions.saveCheckpoint = true;
    RunResult paused = executeRun(config, clock, pausedOptions);
    copyFinalFrame(paused, pausedOptions.directory);
    writeRunManifest(pausedOptions.directory, paused, clock, "paused",
                     false, "", "", 0);

    CheckpointData checkpoint = loadCheckpoint(paused.checkpointPath);
    if (dumpJson(checkpoint.raw.get("device_clocks") == nullptr
                     ? Json()
                     : *checkpoint.raw.get("device_clocks"),
                 false) != dumpJson(clockModelToJson(clock), false)) {
        throw std::runtime_error("checkpoint device clocks do not match the capture model");
    }

    std::string checkpointDisplay = relativePathOrSelf(paused.checkpointPath, root);
    RunOptions resumedOptions;
    resumedOptions.directory = joinPath(root, "resumed");
    resumedOptions.mode = "resumed";
    resumedOptions.startTask = checkpoint.nextTaskIndex;
    resumedOptions.stopTask = totalTasks;
    resumedOptions.resume = true;
    resumedOptions.checkpointPath = checkpointDisplay;
    resumedOptions.checkpointSha256 = checkpoint.sha256;
    resumedOptions.resumedFromTask = checkpoint.nextTaskIndex;
    resumedOptions.initialEvents = checkpoint.events;
    resumedOptions.fixedPlan = &checkpoint.plan;
    resumedOptions.seededFrames = checkpoint.frames;
    resumedOptions.seedFrameDirectory = pausedOptions.directory;
    RunResult resumed = executeRun(checkpoint.config, clock, resumedOptions);
    copyFinalFrame(resumed, resumedOptions.directory);
    writeRunManifest(resumedOptions.directory, resumed, clock, "resumed",
                     true, checkpointDisplay, checkpoint.sha256,
                     checkpoint.nextTaskIndex);

    Json comparison = buildComparison(baselineOptions.directory, resumedOptions.directory,
                                      baseline.frames, resumed.frames);
    bool comparisonPassed = comparison.get("summary") != nullptr &&
                            comparison.get("summary")->get("bitwise_equal") != nullptr &&
                            comparison.get("summary")->get("bitwise_equal")->asBool();
    writeJsonFile(joinPath(root, "comparison.json"), comparison);

    std::vector<RunResult> pressureRuns;
    uint64_t rowBytes = static_cast<uint64_t>(config.width) * 4ULL;
    uint64_t bandRows = static_cast<uint64_t>(std::max(1, std::min(config.height - 1, 8)));
    Config bandedConfig = config;
    bandedConfig.backend = "auto";
    bandedConfig.memoryBudgetBytes = rowBytes * bandRows;
    RunOptions bandedOptions;
    bandedOptions.directory = joinPath(root, "pressure-banded");
    bandedOptions.mode = "pressure-banded";
    bandedOptions.stopTask = std::min(totalTasks, 4);
    pressureRuns.push_back(executeRun(bandedConfig, clock, bandedOptions));
    copyFinalFrame(pressureRuns.back(), bandedOptions.directory);
    writeRunManifest(bandedOptions.directory, pressureRuns.back(), clock,
                     "pressure-banded", false, "", "", 0);

    Config cpuConfig = config;
    cpuConfig.backend = "auto";
    cpuConfig.memoryBudgetBytes = std::max<uint64_t>(1, rowBytes / 2);
    RunOptions cpuOptions;
    cpuOptions.directory = joinPath(root, "pressure-cpu");
    cpuOptions.mode = "pressure-cpu";
    cpuOptions.stopTask = std::min(totalTasks, 4);
    pressureRuns.push_back(executeRun(cpuConfig, clock, cpuOptions));
    copyFinalFrame(pressureRuns.back(), cpuOptions.directory);
    writeRunManifest(cpuOptions.directory, pressureRuns.back(), clock,
                     "pressure-cpu", false, "", "", 0);

    writeEvidence(root, baseline, paused, resumed, pressureRuns, clock,
                  invokedCommand, comparisonPassed);

    std::string report;
    bool verified = verifyArtifactRoot(root, report);
    std::cout << report;
    if (verified) {
        std::cout << "artifact_root: " << root << '\n'
                  << "evidence: " << joinPath(root, "evidence.json") << '\n'
                  << "review: " << joinPath(root, "review.md") << '\n';
    }
    if (!verified) return false;
    if (options.preview) previewImage(joinPath(resumedOptions.directory, "final.ppm"));
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        CliOptions options = parseArguments(argc, argv);
        if (options.help) {
            printHelp();
            return 0;
        }
        if (options.command == "verify") {
            if (options.argument.empty()) throw std::runtime_error("verify requires an artifact directory");
            std::string report;
            bool passed = verifyArtifactRoot(options.argument, report);
            std::cout << report;
            return passed ? 0 : 2;
        }
        if (options.command == "inspect") {
            if (options.argument.empty()) throw std::runtime_error("inspect requires an artifact directory");
            printArtifactSummary(options.argument);
            return 0;
        }
        if (options.command == "preview") {
            if (options.argument.empty()) throw std::runtime_error("preview requires a PPM file");
            return previewImage(options.argument) ? 0 : 3;
        }
        if (options.command == "resume") {
            return runResume(options) ? 0 : 2;
        }
        return runSuite(options, commandLine(argc, argv)) ? 0 : 2;
    } catch (const std::exception& exception) {
        std::cerr << "error: " << exception.what() << '\n';
        return 1;
    }
}

