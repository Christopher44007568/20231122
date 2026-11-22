#include "Scenarios.hpp"
#include "VulkanApp.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace {

void printUsage(const char* executable) {
    std::cout
        << "Usage: " << executable << " [options]\n"
        << "  --headless            Run deterministic page-paging validation\n"
        << "  --scenario NAME       empty | duplicate | cross (headless only)\n"
        << "  --summary             Omit per-switch rows in headless mode\n"
        << "  --help                Show this help\n"
        << "GUI controls: SPACE play/pause, LEFT/RIGHT step, 1/2/3 scenarios,\n"
        << "              R reset, +/- speed, ESC quit\n";
}

}  // namespace

int main(int argc, char** argv) {
    bool headless = false;
    bool summary = false;
    std::string scenarioFilter;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--headless" || argument == "--validate") {
            headless = true;
        } else if (argument == "--summary") {
            summary = true;
            headless = true;
        } else if (argument == "--scenario" && i + 1 < argc) {
            scenarioFilter = argv[++i];
            headless = true;
        } else if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argument << "\n";
            printUsage(argv[0]);
            return 2;
        }
    }

    if (!headless) {
        try {
            tpd::VulkanApp app;
            return app.run();
        } catch (const std::exception& error) {
            std::cerr << "Fatal error: " << error.what() << "\n";
            return 1;
        }
    }

    const tpd::PagingConfig config;
    std::vector<tpd::Scenario> scenarios = tpd::makeScenarios();
    if (!scenarioFilter.empty()) {
        scenarios.erase(
            std::remove_if(scenarios.begin(), scenarios.end(),
                           [&scenarioFilter](const tpd::Scenario& scenario) {
                               return scenario.id != scenarioFilter;
                           }),
            scenarios.end());
        if (scenarios.empty()) {
            std::cerr << "Unknown scenario: " << scenarioFilter << "\n";
            return 2;
        }
    }

    bool passed = true;
    for (const tpd::Scenario& scenario : scenarios) {
        const tpd::ScenarioReport report =
            tpd::runScenario(scenario, config);
        std::cout << tpd::formatScenarioReport(report, config, !summary) << "\n";
        passed = passed && report.passed;
    }
    if (passed) {
        std::cout << (scenarios.size() == 1 ? "SCENARIO PASS"
                                            : "ALL SCENARIOS PASS")
                  << "\n";
    } else {
        std::cout << "SCENARIO FAILURES\n";
    }
    return passed ? 0 : 1;
}
