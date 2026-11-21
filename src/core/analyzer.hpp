#pragma once

#include "parser.hpp"
#include "types.hpp"

#include <string>

namespace gcdump {

struct ExtensionInfo {
    std::string name;
    std::string status;
    std::string rawValue;
    std::size_t sourceLine = 0;
};

// Runs the full six-stage provenance pipeline over a parsed dump and returns
// a report in which every finding cites source lines and pipeline steps.
Report analyze(ParseResult parsed, std::string sourcePath);

std::vector<ExtensionInfo> extractExtensions(const Report& report);

} // namespace gcdump
