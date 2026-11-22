#include "Scenarios.hpp"

#include <iostream>

int main() {
    tpd::PagingConfig config;
    const auto reports = tpd::runAllScenarios(config);
    bool passed = true;
    for (const auto& report : reports) {
        std::cout << tpd::formatScenarioReport(report, config, true) << "\n";
        passed = passed && report.passed;
    }
    std::cout << (passed ? "ALL SCENARIOS PASS" : "SCENARIO FAILURES") << "\n";
    return passed ? 0 : 1;
}
