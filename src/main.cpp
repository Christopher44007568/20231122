#include "experiment.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "  --record <path>   canonical event record (default data/canonical.record)\n"
              << "  --out <dir>       evidence directory (default artifacts/offscreen-consistency)\n"
              << "  --width <pixels>  offscreen width (default 320)\n"
              << "  --height <pixels> offscreen height (default 200)\n"
              << "  --no-double-capture  disable exact repeat-render check\n"
              << "  --help\n";
}

}  // namespace

int main(int argc, char** argv) {
    offscreen::ExperimentOptions options;
    options.recordPath = "data/canonical.record";
    options.outputDirectory = "artifacts/offscreen-consistency";

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto next = [&](const char* name) -> std::string {
                if (i + 1 >= argc) {
                    throw std::runtime_error(std::string("missing value for ") + name);
                }
                return argv[++i];
            };
            if (arg == "--record") options.recordPath = next("--record");
            else if (arg == "--out") options.outputDirectory = next("--out");
            else if (arg == "--width")
                options.width = static_cast<std::uint32_t>(std::stoul(next("--width")));
            else if (arg == "--height")
                options.height = static_cast<std::uint32_t>(std::stoul(next("--height")));
            else if (arg == "--no-double-capture") options.doubleCapture = false;
            else if (arg == "--help" || arg == "-h") {
                usage(argv[0]);
                return 0;
            } else {
                throw std::runtime_error("unknown argument: " + arg);
            }
        }
        return offscreen::runExperiment(options);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
