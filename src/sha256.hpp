#pragma once

#include <string>
#include <string_view>

namespace bbox {

std::string sha256Hex(std::string_view data);
std::string sha256File(const std::string& path);

}  // namespace bbox

