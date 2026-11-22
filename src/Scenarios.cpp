#include "Scenarios.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>

namespace tpd {
namespace {

std::string twoDigits(int value) {
    std::ostringstream out;
    out << std::setw(2) << std::setfill('0') << value;
    return out.str();
}

std::string layerText(int layer) {
    return layer < 0 ? "--" : "L" + twoDigits(layer);
}

std::string yesNo(bool value) {
    return value ? "yes" : "no";
}

}  // namespace

std::vector<PageDefinition> makeDefaultPages() {
    std::vector<PageDefinition> pages;
    pages.reserve(12);
    for (int page = 0; page < 12; ++page) {
        PageDefinition definition;
        definition.label = "P" + twoDigits(page);
        if (page == 2 || page == 7) {
            definition.empty = true;
            definition.contentId = -1;
        } else if (page == 9) {
            definition.contentId = 4;
        } else {
            definition.contentId = page;
        }
        pages.push_back(definition);
    }
    return pages;
}

std::vector<Scenario> makeScenarios() {
    return {
        {"empty", "EMPTY PAGE FALLBACK", {0, 2, 3, 7, 0, 2, 5, 2, 7, 1}, 6},
        {"duplicate", "DUPLICATE CONTENT SHARING", {4, 9, 4, 5, 9, 4, 9, 5, 9}, 5},
        {"cross", "CONTINUOUS CROSS-PAGE ACCESS",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
          0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
         2},
    };
}

ScenarioReport runScenario(const Scenario& scenario, const PagingConfig& config) {
    ScenarioReport report;
    report.scenario = scenario;
    PagingModel model(config, makeDefaultPages());
    std::map<int, int> layerContent;
    int maxLayerUsed = kInvalidLayer;

    uint64_t tick = 0;
    for (int pageId : scenario.pages) {
        const SwitchTrace trace = model.requestPage(pageId, tick);
        const PageEntry& page = model.page(pageId);
        if (page.empty && model.sampleLayer(pageId) != kInvalidLayer) {
            report.errors.push_back("empty page sampled a stale layer at request " +
                                    std::to_string(trace.seq));
        }
        if (!page.empty && !trace.resolved &&
            model.sampleLayer(pageId) != kInvalidLayer) {
            report.errors.push_back("pending page sampled a stale layer at request " +
                                    std::to_string(trace.seq));
        }
        model.takeStartedUploads();

        const uint64_t nextTick = tick + scenario.stepTicks;
        model.advanceTo(nextTick);
        tick = nextTick;
        const std::vector<std::string> stepErrors = model.validateInvariants();
        report.errors.insert(report.errors.end(), stepErrors.begin(),
                             stepErrors.end());
    }

    for (int guard = 0; guard < 512 && model.pendingTrace() != nullptr; ++guard) {
        ++tick;
        model.advanceTo(tick);
    }

    report.traces = model.traces();
    report.uploadCount = static_cast<uint32_t>(model.uploads().size());
    for (const SwitchTrace& trace : report.traces) {
        if (!trace.resolved) {
            report.errors.push_back("unresolved switch request " +
                                    std::to_string(trace.seq));
            continue;
        }
        if (trace.outcome == RequestOutcome::DuplicateHit) {
            ++report.duplicateHits;
        }
        if (trace.outcome == RequestOutcome::EmptyPage) {
            ++report.emptyHits;
        }
        if (trace.finalLayer >= 0) {
            auto found = layerContent.find(trace.finalLayer);
            if (found != layerContent.end() && found->second != trace.contentId) {
                ++report.layerWraps;
            }
            layerContent[trace.finalLayer] = trace.contentId;
            maxLayerUsed = std::max(maxLayerUsed, trace.finalLayer);
        }
    }

    const std::vector<std::string> invariantErrors = model.validateInvariants();
    report.errors.insert(report.errors.end(), invariantErrors.begin(),
                         invariantErrors.end());

    if (scenario.id == "empty") {
        for (const SwitchTrace& trace : report.traces) {
            if (trace.empty && trace.finalLayer != kInvalidLayer) {
                report.errors.push_back("empty request returned a layer: " +
                                        std::to_string(trace.seq));
            }
        }
    } else if (scenario.id == "duplicate") {
        int firstLayer = kInvalidLayer;
        int duplicateLayer = kInvalidLayer;
        for (const SwitchTrace& trace : report.traces) {
            if (trace.pageId == 4 && trace.resolved) {
                firstLayer = trace.finalLayer;
            }
            if (trace.pageId == 9 && trace.resolved) {
                duplicateLayer = trace.finalLayer;
            }
        }
        if (firstLayer < 0 || firstLayer != duplicateLayer) {
            report.errors.push_back("duplicate pages did not share a layer");
        }
        if (report.duplicateHits == 0) {
            report.errors.push_back("duplicate content never produced a shared hit");
        }
        for (const UploadItem& upload : model.uploads()) {
            if (upload.pageId == 9) {
                report.errors.push_back(
                    "duplicate page P09 was uploaded instead of shared");
            }
        }
    } else if (scenario.id == "cross") {
        if (report.traces.size() != scenario.pages.size()) {
            report.errors.push_back("cross-page requests were dropped");
        }
        for (const SwitchTrace& trace : report.traces) {
            if (trace.finalLayer < kInvalidLayer ||
                trace.finalLayer >= config.layerCount) {
                report.errors.push_back("cross-page final layer outside array: " +
                                        std::to_string(trace.seq));
            }
        }
        if (report.layerWraps == 0) {
            report.errors.push_back("cross-page replay never wrapped a layer");
        }
        if (maxLayerUsed != config.layerCount - 1) {
            report.errors.push_back(
                "cross-page replay did not reach the final array layer");
        }
    }
    for (const UploadItem& upload : model.uploads()) {
        if (model.page(upload.pageId).empty) {
            report.errors.push_back("empty page entered the upload queue: " +
                                    std::to_string(upload.pageId));
        }
    }

    report.passed = report.errors.empty();
    return report;
}

std::vector<ScenarioReport> runAllScenarios(const PagingConfig& config) {
    std::vector<ScenarioReport> reports;
    for (const Scenario& scenario : makeScenarios()) {
        reports.push_back(runScenario(scenario, config));
    }
    return reports;
}

std::string formatSwitchLine(const SwitchTrace& trace, const PagingConfig& config) {
    std::ostringstream out;
    const double waitMs = static_cast<double>(trace.waitTicks()) * config.frameMs;
    out << "seq=" << std::setw(3) << std::setfill('0') << trace.seq
        << " page=P" << twoDigits(trace.pageId)
        << " content="
        << (trace.empty ? "EMPTY" : "C" + twoDigits(trace.contentId))
        << " request_tick=" << trace.requestTick
        << " resolve_tick=" << trace.resolveTick
        << " wait=" << trace.waitTicks() << "t/" << std::fixed
        << std::setprecision(1) << waitMs << "ms"
        << " hit_layer=" << layerText(trace.hitLayer)
        << " final_sample_layer="
        << (trace.empty ? "EMPTY" : layerText(trace.finalLayer))
        << " outcome=" << toString(trace.outcome)
        << " empty=" << yesNo(trace.empty)
        << " duplicate=" << yesNo(trace.duplicate);
    return out.str();
}

std::string formatScenarioReport(const ScenarioReport& report,
                                 const PagingConfig& config,
                                 bool verbose) {
    std::ostringstream out;
    out << "scenario=" << report.scenario.id << " \"" << report.scenario.title
        << "\" pages=" << report.scenario.pages.size()
        << " uploads=" << report.uploadCount
        << " duplicate_hits=" << report.duplicateHits
        << " empty_hits=" << report.emptyHits
        << " layer_wraps=" << report.layerWraps << "\n";
    if (verbose) {
        for (const SwitchTrace& trace : report.traces) {
            out << "  " << formatSwitchLine(trace, config) << "\n";
        }
    }
    out << "validation=" << (report.passed ? "PASS" : "FAIL");
    if (!report.passed) {
        out << "\n";
        for (const std::string& error : report.errors) {
            out << "  error: " << error << "\n";
        }
    }
    return out.str();
}

}  // namespace tpd
