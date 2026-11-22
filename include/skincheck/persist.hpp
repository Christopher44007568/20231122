#pragma once

#include <string>
#include <vector>

#include "skincheck/types.hpp"

namespace skincheck {

struct RunSnapshot {
    std::string schema = "skincheck.run/v1";
    std::string run_id;
    std::string created_utc;
    std::string phase = "complete";
    Capture capture;
    ResourcePolicy policy;
    ResourcePlan resources;
    Timeline timeline;
    Checkpoint checkpoint;
    bool has_checkpoint = false;
    std::vector<FrameResult> before_frames;
    std::vector<FrameResult> final_frames;
    ResumeComparison comparison;
};

struct ExportResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> files;
};

struct VerifyResult {
    bool ok = false;
    int checks = 0;
    int failures = 0;
    std::string report_path;
    std::vector<std::string> messages;
};

Json capture_to_json(const Capture& capture);
Capture capture_from_json(const Json& json);
Json frame_to_json(const FrameResult& frame);
FrameResult frame_from_json(const Json& json);
Json checkpoint_to_json(const Checkpoint& checkpoint);
Checkpoint checkpoint_from_json(const Json& json);
Json run_to_json(const RunSnapshot& snapshot,
                 const std::vector<std::pair<std::string, std::string>>& artifacts);

ExportResult export_run(const std::string& directory, const RunSnapshot& snapshot);
VerifyResult verify_run(const std::string& directory);

const std::vector<std::string>& run_field_order();
const std::vector<std::string>& checkpoint_field_order();
const std::vector<std::string>& frame_field_order();
const std::vector<std::string>& issue_field_order();

}  // namespace skincheck
