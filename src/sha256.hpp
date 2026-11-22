#pragma once

#include "common.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace offscreen {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t size);
    std::string hexDigest();

private:
    void transform(const std::uint8_t* block);

    std::uint32_t state_[8];
    std::uint8_t buffer_[64];
    std::size_t buffered_;
    std::uint64_t total_;
};

std::string sha256Hex(const void* data, std::size_t size);
std::string sha256Hex(const Bytes& bytes);
std::string sha256Hex(std::string_view text);

}  // namespace offscreen
