#include "surface_provenance.h"
#include "vulkan_app.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void printUsage() {
    std::cout
        << "曲面参数化展开 · 来源检查页\n"
        << "  surface_unfold [--root DIR] [--select ID] [--screenshot PNG]\n"
        << "                 [--frames N] [--verify] [--help]\n";
}

}  // namespace

int main(int argc, char** argv) {
    surfprov::AppOptions options;
    options.root = SURFPROV_SOURCE_DIR;
    bool verifyOnly = false;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto next = [&]() -> std::string {
            if (index + 1 >= argc) {
                throw std::runtime_error("missing value for " + arg);
            }
            return argv[++index];
        };
        try {
            if (arg == "--root") {
                options.root = next();
            } else if (arg == "--select") {
                options.initialRecord = next();
            } else if (arg == "--screenshot") {
                options.screenshot = next();
            } else if (arg == "--frames") {
                options.autoExitFrames = std::stoi(next());
            } else if (arg == "--verify") {
                verifyOnly = true;
            } else if (arg == "--help" || arg == "-h") {
                printUsage();
                return 0;
            } else {
                std::cerr << "unknown argument: " << arg << '\n';
                printUsage();
                return 2;
            }
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 2;
        }
    }

    if (!options.screenshot.empty()) {
        if (options.autoExitFrames == 0) options.autoExitFrames = 10;
        if (options.autoExitFrames <= options.screenshotFrame) {
            options.screenshotFrame = std::max(2, options.autoExitFrames - 2);
        }
    }

    try {
        if (verifyOnly) {
            const auto batch = surfprov::processBatch({options.root, true});
            std::cout << "objects=" << batch.records.size()
                      << " snapshots=" << batch.snapshotCount
                      << " valid_snapshots=" << batch.validSnapshotCount
                      << " pass=" << batch.passCount
                      << " failed=" << batch.failedCount
                      << " undecidable=" << batch.undecidableCount
                      << " boundary=" << batch.boundaryCount
                      << " continued="
                      << (batch.continuedAfterException ? "yes" : "no")
                      << " temp_cleaned="
                      << (batch.tempCleaned ? "yes" : "no") << '\n';
            const bool okay =
                batch.records.size() == 3 && batch.snapshotCount == 2 &&
                batch.validSnapshotCount == 2 && batch.failedCount == 1 &&
                batch.undecidableCount == 1 && batch.boundaryCount == 1 &&
                batch.exceptionCaught && batch.continuedAfterException &&
                batch.tempCleaned && !std::filesystem::exists(batch.tempPath);
            return okay ? 0 : 1;
        }
        return surfprov::runVulkanApp(options);
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
