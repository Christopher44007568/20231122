#pragma once

#include "rtsync/json_util.hpp"
#include "rtsync/types.hpp"

#include <string>

namespace rtsync {

struct ReportContext {
  std::string artifact_dir;
  std::string input_copy;
  std::string input_prefix_copy;
  std::string parse_error;
  Json evidence_index = Json::object();
  std::string records_sha256;
  bool input_complete = true;
};

Json analysis_json(const Analysis& analysis, const ReportContext& context);
std::string review_markdown(const Analysis& analysis,
                            const ReportContext& context);
Json manifest_json(const Analysis& analysis, const ReportContext& context,
                   const Json& files);
Json records_jsonl(const Analysis& analysis);

}  // namespace rtsync

