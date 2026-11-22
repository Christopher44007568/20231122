#pragma once

#include "vegetation.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace vegd {

using Json = nlohmann::ordered_json;

std::string utcNow();
bool writeJsonFile(const std::string& path, const Json& value,
                   std::string& error);
bool readJsonFile(const std::string& path, Json& value, std::string& error);
Json sampleJson(const BoundarySample& sample);
Json degradationsJson(const std::vector<std::string>& degradations);

}  // namespace vegd
