#include "experiment.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void usage() {
    std::cout
        << "usage:\n"
        << "  instance_select run [--input DIR] [--out DIR] [--seed N]\n"
        << "  instance_select replay --trace FILE [--out FILE]\n";
}

std::string requireValue(int& index, int argc, char** argv,
                         const std::string& option) {
    if (index + 1 >= argc) {
        throw std::runtime_error(option + " requires a value");
    }
    return argv[++index];
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            usage();
            return 2;
        }
        const std::string command = argv[1];
        if (command == "run") {
            inst::ExperimentOptions options;
            for (int i = 2; i < argc; ++i) {
                const std::string argument = argv[i];
                if (argument == "--input") {
                    options.inputDir =
                        requireValue(i, argc, argv, argument);
                } else if (argument == "--out") {
                    options.outputDir =
                        requireValue(i, argc, argv, argument);
                } else if (argument == "--seed") {
                    options.shuffleSeed = static_cast<uint32_t>(
                        std::stoul(requireValue(i, argc, argv, argument),
                                    nullptr, 0));
                } else if (argument == "--help" || argument == "-h") {
                    usage();
                    return 0;
                } else {
                    throw std::runtime_error("unknown option: " + argument);
                }
            }
            return inst::runExperiment(options);
        }
        if (command == "replay") {
            inst::fs::path trace = "artifacts/evidence/trace.jsonl";
            inst::fs::path output = "artifacts/evidence/replay/report.json";
            for (int i = 2; i < argc; ++i) {
                const std::string argument = argv[i];
                if (argument == "--trace" || argument == "--journal") {
                    trace = requireValue(i, argc, argv, argument);
                } else if (argument == "--out") {
                    output = requireValue(i, argc, argv, argument);
                } else if (argument == "--help" || argument == "-h") {
                    usage();
                    return 0;
                } else {
                    throw std::runtime_error("unknown option: " + argument);
                }
            }
            const int mismatches = inst::replayTrace(trace, output);
            std::cout << "[replay] mismatches=" << mismatches
                      << " report=" << output.string() << '\n';
            return mismatches == 0 ? 0 : 1;
        }
        if (command == "--help" || command == "-h" || command == "help") {
            usage();
            return 0;
        }
        throw std::runtime_error("unknown command: " + command);
    } catch (const std::exception& exception) {
        std::cerr << "[instance-select] " << exception.what() << '\n';
        return 3;
    }
}
