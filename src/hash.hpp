#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace msphere {

std::string sha256_hex(const uint8_t* data, size_t size);
std::string sha256_hex(const std::string& text);

}  // namespace msphere
