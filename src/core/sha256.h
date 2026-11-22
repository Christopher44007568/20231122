#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ssor {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t size);
    void update(const std::string& text);
    std::string finishHex();

private:
    void transform(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_;
    std::uint64_t bitCount_;
    std::array<std::uint8_t, 64> buffer_;
    std::size_t bufferSize_;
    bool finished_;
};

}  // namespace ssor
