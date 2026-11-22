#pragma once

#include "compare.hpp"
#include "params.hpp"
#include "session.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct CandidateEvaluation {
  BiasCandidate bias;
  CompareResult compare;
  double score = 0.0;
  bool selected = false;
};

bool writeObjectEvidence(const std::filesystem::path& directory,
                         const ObjectRun& run, const DepthParams& preset,
                         const std::vector<CandidateEvaluation>& candidates,
                         const std::filesystem::path& baselineImage,
                         std::string& error);

