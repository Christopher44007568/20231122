#pragma once

#include "json.hpp"

#include <string>

namespace vegd {

bool appendEvidence(const std::string& path, const std::string& event,
                    const Json& fields, std::string& error);
bool resetEvidence(const std::string& path, std::string& error);

}  // namespace vegd
