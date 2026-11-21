#pragma once

#include "core/workspace.h"

#include <nlohmann/json.hpp>

namespace clipfix::detail {

nlohmann::json vec3Json(const Vec3& value);
nlohmann::json statsJson(const RunStats& stats);
nlohmann::json runJson(const RunRecord& run);
nlohmann::json compareJson(const CompareResult& compare);
nlohmann::json exportJson(const ExportRecord& exportRecord);
nlohmann::json objectJson(const ObjectState& object);
nlohmann::json sessionJson(const SessionState& state);

RunRecord runFromJson(const nlohmann::json& value);
CompareResult compareFromJson(const nlohmann::json& value);
ExportRecord exportFromJson(const nlohmann::json& value);
ObjectState objectFromJson(const nlohmann::json& value);
SessionState sessionFromJson(const nlohmann::json& value);

}  // namespace clipfix::detail
