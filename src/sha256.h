#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ph {

std::string sha256Hex(const std::string& input);
std::string sha256Hex(const std::vector<uint8_t>& input);
std::string bytesToHex(const std::vector<uint8_t>& bytes);

}  // namespace ph
