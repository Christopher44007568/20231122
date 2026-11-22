#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nmts {

bool write_atomic(const std::string& path, const std::vector<std::uint8_t>& bytes);
bool write_text_atomic(const std::string& path, const std::string& text);
bool read_file(const std::string& path, std::vector<std::uint8_t>& out);
bool ensure_dir(const std::string& path);
bool file_exists(const std::string& path);
std::string join_path(const std::string& a, const std::string& b);
std::string json_escape(const std::string& s);
std::string format_double(double v, int digits = 9);

}  // namespace nmts
