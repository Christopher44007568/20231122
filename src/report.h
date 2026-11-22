#pragma once
#include <string>
#include <vector>
#include "analyze.h"
#include "gpu.h"
#include "log.h"

namespace report {

struct Step {
    std::string id;
    std::string name;
    std::string detail;
};

struct Data {
    std::string run_id;
    std::string generated_at;
    gpu::DeviceInfo device;
    uint64_t budget_ns = 0;
    std::string log_path;
    std::string log_sha;        // fnv1a64 of final log bytes
    std::string truncation_evidence_path;
    std::string truncation_salvaged_hex;
    analyze::Analysis analysis;
    analyze::TruncationEvent truncation;
    std::vector<tslog::Record> records;
    std::vector<Step> steps;
};

// Renders the self-contained HTML inspection page.
std::string render_html(const Data& d);

}  // namespace report
