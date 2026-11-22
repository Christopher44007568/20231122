#pragma once

#include <string>

namespace skincheck {

std::string sha256_hex(const std::string& bytes);
std::string sha256_file_hex(const std::string& path, std::string* error = nullptr);

}  // namespace skincheck
