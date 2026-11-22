#include "runner.hpp"
#include "sample_cases.hpp"

#include "platform.hpp"

#include <chrono>
#include <ctime>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kUsage =
    "Usage:\n"
    "  raster-regression --case INPUT.json --out DIR [options]\n"
    "  raster-regression --stress --out DIR [options]\n"
    "\n"
    "Options:\n"
    "  --max-input-bytes N   reject inputs larger than N bytes (default 67108864)\n"
    "  --device-name TEXT    prefer a Vulkan device containing TEXT\n"
    "  --device-index N      select the Nth Vulkan physical device\n"
    "  --validation          enable Khronos validation when available\n"
    "  --preview             keep a GLFW preview window open until closed\n"
    "  --help                show this text\n";

struct CliOptions {
    std::optional<std::filesystem::path> input;
    std::filesystem::path output = "artifacts/regression-run";
    rr::Limits limits;
    rr::RenderOptions render;
    bool stress = false;
    bool preview = false;
    bool maxInputSet = false;
    bool help = false;
};

bool parseUnsigned(const std::string& text, std::uint64_t& value) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    value = parsed;
    return true;
}

std::optional<CliOptions> parseArguments(int argc, char** argv, std::string& error) {
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto next = [&](std::string& value) -> bool {
            if (i + 1 >= argc) {
                error = "missing value for " + argument;
                return false;
            }
            value = argv[++i];
            return true;
        };
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--stress") {
            options.stress = true;
        } else if (argument == "--preview") {
            options.preview = true;
        } else if (argument == "--validation") {
            options.render.enableValidation = true;
        } else if (argument == "--case" || argument == "--input") {
            std::string value;
            if (!next(value)) return std::nullopt;
            options.input = value;
        } else if (argument == "--out") {
            std::string value;
            if (!next(value)) return std::nullopt;
            options.output = value;
        } else if (argument == "--max-input-bytes") {
            std::string value;
            if (!next(value)) return std::nullopt;
            std::uint64_t parsed = 0;
            if (!parseUnsigned(value, parsed) || parsed < 1024U) {
                error = "--max-input-bytes must be an integer >= 1024";
                return std::nullopt;
            }
            options.limits.max_input_bytes = static_cast<std::size_t>(parsed);
            options.maxInputSet = true;
        } else if (argument == "--device-name") {
            if (!next(options.render.preferredDeviceName)) return std::nullopt;
        } else if (argument == "--device-index") {
            std::string value;
            if (!next(value)) return std::nullopt;
            std::uint64_t parsed = 0;
            if (!parseUnsigned(value, parsed) ||
                parsed > static_cast<std::uint64_t>(INT32_MAX)) {
                error = "--device-index is out of range";
                return std::nullopt;
            }
            options.render.preferredDeviceIndex = static_cast<int>(parsed);
        } else {
            error = "unknown argument: " + argument;
            return std::nullopt;
        }
    }
    if (!options.help && options.stress == options.input.has_value()) {
        error = "choose exactly one of --case INPUT or --stress";
        return std::nullopt;
    }
    return options;
}

int runStress(const CliOptions& options) {
    rr::Limits limits = options.limits;
    if (!options.maxInputSet) {
        limits.max_input_bytes = 1024U * 1024U;
    }
    const auto inputDirectory = options.output / "generated-inputs";
    std::string error;
    const auto empty = inputDirectory / "empty.json";
    const auto large = inputDirectory / "large-valid.json";
    const auto oversized = inputDirectory / "oversized.json";
    if (!rr::writeEmptyInput(empty, error) ||
        !rr::writeLargeValidInput(large, 8000U, error) ||
        !rr::writeOversizedInput(oversized, limits.max_input_bytes, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    const auto fixtures = rr::bundledFixturesDirectory();
    struct Case {
        std::string id;
        std::filesystem::path input;
        std::string expected;
    };
    const std::vector<Case> cases{
        {"empty", empty, "ok"},
        {"shared-edge", fixtures / "shared_edge.json", "ok"},
        {"boundary-line-width", fixtures / "boundary_line_width.json", "ok"},
        {"boundary-viewport-x", fixtures / "boundary_viewport_x.json", "ok"},
        {"extreme", fixtures / "extreme.json", "ok"},
        {"large-valid", large, "ok"},
        {"oversized", oversized, "input_rejected"},
    };

    int failures = 0;
    std::ostringstream summary;
    summary << "{\n  \"schema\": \"raster-regression-stress/v1\",\n  \"cases\": [\n";
    for (std::size_t i = 0; i < cases.size(); ++i) {
        rr::RunOptions run;
        run.inputPath = cases[i].input;
        run.outputDirectory = options.output / cases[i].id;
        run.limits = limits;
        run.render = options.render;
        const rr::RunSummary result = rr::runRegression(run);
        const bool pass = result.status == cases[i].expected;
        if (!pass) {
            ++failures;
        }
        std::cout << "stress=" << cases[i].id << " expected="
                  << cases[i].expected << " actual=" << result.status
                  << " ref_failures=" << result.referenceFailures << '\n';
        summary << "    {\"id\": \"" << cases[i].id << "\", \"expected\": \""
                << cases[i].expected << "\", \"actual\": \"" << result.status
                << "\", \"pass\": " << (pass ? "true" : "false")
                << ", \"output\": \"" << (options.output / cases[i].id).string()
                << "\", \"reference_failures\": " << result.referenceFailures
                << "}";
        if (i + 1U != cases.size()) {
            summary << ',';
        }
        summary << '\n';
    }
    summary << "  ],\n  \"failures\": " << failures << "\n}\n";
    std::filesystem::create_directories(options.output);
    std::ofstream stream(options.output / "stress-summary.json",
                         std::ios::binary | std::ios::trunc);
    stream << summary.str();
    if (!stream) {
        std::cerr << "cannot write stress summary\n";
        return 1;
    }
    std::cout << "stress-summary=" << (options.output / "stress-summary.json").string()
              << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}

void showPreview(const rr::RunSummary& summary) {
    rr::PreviewWindow window =
        rr::openPreviewWindow("raster-regression: " + summary.status, 720, 420);
    if (!window.open) {
        std::cerr << "preview unavailable: " << window.error << '\n';
        return;
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!rr::previewWindowShouldClose(window) &&
           std::chrono::steady_clock::now() < deadline) {
        rr::pollPreviewWindow(window);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    rr::closePreviewWindow(window);
}

}  // namespace

int main(int argc, char** argv) {
    std::string error;
    std::optional<CliOptions> options = parseArguments(argc, argv, error);
    if (!options) {
        std::cerr << error << "\n\n" << kUsage;
        return 64;
    }
    if (options->help) {
        std::cout << kUsage;
        return 0;
    }
    if (options->stress) {
        const int code = runStress(*options);
        rr::shutdownPlatform();
        return code;
    }
    rr::RunOptions run;
    run.inputPath = *options->input;
    run.outputDirectory = options->output;
    run.limits = options->limits;
    run.render = options->render;
    const rr::RunSummary summary = rr::runRegression(run);
    std::cout << "status=" << summary.status << " ref_failures="
              << summary.referenceFailures << " out=" << summary.outputDirectory
              << '\n';
    if (options->preview) {
        showPreview(summary);
    }
    rr::shutdownPlatform();
    return summary.exitCode;
}
