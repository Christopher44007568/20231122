#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace vfcr {

std::string sha256(std::string_view bytes);
std::uint64_t fnv1a64(std::string_view bytes);
std::string shortDigest(std::string_view digest, std::size_t length = 12U);

}  // namespace vfcr
