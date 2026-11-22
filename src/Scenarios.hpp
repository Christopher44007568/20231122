#pragma once

#include "PagingModel.hpp"

#include <string>
#include <vector>

namespace tpd {

struct Scenario {
    std::string id;
    std::string title;
    std::vector<int> pages;
    uint32_t stepTicks = 6;
};

struct ScenarioReport {
    Scenario scenario;
    std::vector<SwitchTrace> traces;
    std::vector<std::string> errors;
    uint32_t uploadCount = 0;
    uint32_t duplicateHits = 0;
    uint32_t emptyHits = 0;
    uint32_t layerWraps = 0;
    bool passed = false;
};

std::vector<PageDefinition> makeDefaultPages();
std::vector<Scenario> makeScenarios();
ScenarioReport runScenario(const Scenario& scenario, const PagingConfig& config);
std::vector<ScenarioReport> runAllScenarios(const PagingConfig& config);

std::string formatSwitchLine(const SwitchTrace& trace, const PagingConfig& config);
std::string formatScenarioReport(const ScenarioReport& report,
                                 const PagingConfig& config,
                                 bool verbose);

}  // namespace tpd
