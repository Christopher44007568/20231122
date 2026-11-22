#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace inspect {

class Sha256 {
public:
    Sha256();

    void update(const void* data, std::size_t size);
    void update(const std::vector<std::uint8_t>& data);
    std::array<std::uint8_t, 32> finish();

private:
    void transform(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t bit_count_ = 0;
    std::size_t buffer_size_ = 0;
    bool finished_ = false;
};

std::string sha256Hex(const std::vector<std::uint8_t>& data);
std::string sha256Hex(const void* data, std::size_t size);
std::string sha256FileHex(const std::string& path, std::uint64_t* size_out = nullptr);

}  // namespace inspect
