#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lshc {

uint32_t crc32c(const void* data, std::size_t size);
uint64_t fnv1a64(const void* data, std::size_t size);
std::string sha256Hex(const void* data, std::size_t size);
std::string sha256Hex(const std::vector<uint8_t>& bytes);
std::string sha256File(const std::string& path);
std::string hexBytes(const std::vector<uint8_t>& bytes);

}  // namespace lshc

