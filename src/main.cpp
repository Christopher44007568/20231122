#include <cstdlib>
#include <iostream>
#include <string>

#include "app.hpp"

namespace {

bool parseUnsigned(const std::string& text, std::uint64_t& value) {
    try {
        std::size_t consumed = 0;
        value = std::stoull(text, &consumed);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

bool takeValue(int argc, char** argv, int& index, std::string& value) {
    if (index + 1 >= argc) {
        return false;
    }
    value = argv[++index];
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    inspect::AppOptions options;
    const char* headless_env = std::getenv("GS_HEADLESS");
    options.headless = headless_env != nullptr && std::string(headless_env) != "0";

    int command_count = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            std::cout << inspect::commandHelp();
            return 0;
        }
        std::string value;
        if (argument == "--run" || argument == "--resume" ||
            argument == "--compare" || argument == "--acceptance" ||
            argument == "--gui") {
            if (!takeValue(argc, argv, index, value)) {
                std::cerr << "missing directory for " << argument << '\n';
                return 2;
            }
            if (argument == "--run") {
                options.command = inspect::Command::Run;
            } else if (argument == "--resume") {
                options.command = inspect::Command::Resume;
            } else if (argument == "--compare") {
                options.command = inspect::Command::Compare;
            } else if (argument == "--acceptance") {
                options.command = inspect::Command::Acceptance;
            } else {
                options.command = inspect::Command::Gui;
            }
            options.run_dir = value;
            ++command_count;
            continue;
        }
        if (argument == "--material") {
            if (!takeValue(argc, argv, index, value)) {
                std::cerr << "missing material path\n";
                return 2;
            }
            options.material = value;
            continue;
        }
        if (argument == "--degrade") {
            if (!takeValue(argc, argv, index, value)) {
                std::cerr << "missing degradation mode\n";
                return 2;
            }
            options.requested_mode = value;
            continue;
        }
        if (argument == "--budget-mib") {
            if (!takeValue(argc, argv, index, value)) {
                std::cerr << "missing budget value\n";
                return 2;
            }
            std::uint64_t mib = 0;
            if (!parseUnsigned(value, mib)) {
                std::cerr << "invalid budget\n";
                return 2;
            }
            options.memory_budget_bytes = mib * 1024ULL * 1024ULL;
            continue;
        }
        if (argument == "--width" || argument == "--height") {
            if (!takeValue(argc, argv, index, value)) {
                std::cerr << "missing extent value\n";
                return 2;
            }
            std::uint64_t extent = 0;
            if (!parseUnsigned(value, extent) || extent == 0 ||
                extent > 4096) {
                std::cerr << "invalid extent\n";
                return 2;
            }
            if (argument == "--width") {
                options.width = static_cast<std::uint32_t>(extent);
            } else {
                options.height = static_cast<std::uint32_t>(extent);
            }
            continue;
        }
        if (argument == "--force-low-memory") {
            options.force_low_memory = true;
            continue;
        }
        if (argument == "--headless") {
            options.headless = true;
            continue;
        }
        if (argument == "--verbose") {
            options.verbose = true;
            continue;
        }
        std::cerr << "unknown argument: " << argument << '\n';
        return 2;
    }

    if (command_count > 1) {
        std::cerr << "choose exactly one command\n";
        return 2;
    }
    const std::string mode = options.requested_mode;
    if (mode != "auto" && mode != "full" && mode != "compact" &&
        mode != "cpu") {
        std::cerr << "invalid --degrade mode: " << mode << '\n';
        return 2;
    }
    return inspect::runApplication(options);
}
