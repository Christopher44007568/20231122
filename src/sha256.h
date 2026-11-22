#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

std::string sha256Hex(const void* data, size_t size);
std::string sha256Hex(const std::string& data);
std::string sha256File(const std::string& path);

