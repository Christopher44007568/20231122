#pragma once

#include <cstdint>
#include <string>

namespace mi {

std::string json_escape(const std::string& value);
std::string json_string(const std::string& value);
std::string json_number(double value);
std::string json_bool(bool value);

std::string get_json_string(const std::string& object, const std::string& key,
                            std::string fallback = {});
std::uint64_t get_json_u64(const std::string& object, const std::string& key,
                           std::uint64_t fallback = 0);
double get_json_double(const std::string& object, const std::string& key,
                       double fallback = 0.0);

}  // namespace mi
