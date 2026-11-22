#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace rtsync {

using Json = nlohmann::ordered_json;

bool read_file(const std::string& path, std::string* out, std::string* error);
bool write_file(const std::string& path, const std::string& bytes,
                std::string* error);
bool write_text(const std::string& path, const std::string& text,
                std::string* error);
bool load_json(const std::string& path, Json* out, std::string* error);
bool save_json(const std::string& path, const Json& value, std::string* error);
std::string canonical_json(const Json& value);
bool same_ordered_shape(const Json& left, const Json& right,
                        std::string* difference = nullptr);
std::string utc_now_iso8601();

}  // namespace rtsync

