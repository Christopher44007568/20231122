#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rr {

std::string sha256Hex(const std::uint8_t* data, std::size_t size);
std::string sha256Hex(std::string_view data);
std::string sha256Hex(const std::vector<std::uint8_t>& data);

std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size);
std::string fnv1a64Hex(const std::vector<std::uint8_t>& data);

}  // namespace rr
