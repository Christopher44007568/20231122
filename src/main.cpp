#include "hdr_provenance.h"
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
        << "HDR 纹理格式验证 · 来源检查页\n"
        << "  hdr_provenance [--root DIR] [--select ID] [--screenshot PNG]\n"
        << "                 [--frames N] [--verify] [--help]\n";
}

}  // namespace

int main(int argc, char** argv) {
    hdrprov::AppOptions options;
    options.root = HDRPROV_SOURCE_DIR;
    bool verifyOnly = false;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto next = [&]() -> std::string {
            if (index + 1 >= argc) throw std::runtime_error("missing value for " + arg);
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
            const auto batch = hdrprov::processBatch({options.root, true});
            std::cout << "objects=" << batch.records.size()
                      << " pass=" << batch.passCount
                      << " undecidable=" << batch.undecidableCount
                      << " failed=" << batch.failedCount
                      << " continued=" << (batch.continuedAfterException ? "yes" : "no")
                      << " temp_cleaned=" << (batch.tempCleaned ? "yes" : "no")
                      << '\n';
            return batch.records.size() == 3 && batch.passCount == 2 &&
                           batch.undecidableCount == 1 && batch.tempCleaned
                       ? 0
                       : 1;
        }
        return hdrprov::runVulkanApp(options);
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
