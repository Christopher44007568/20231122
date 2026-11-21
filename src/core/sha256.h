#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace clipfix {

class Sha256 {
public:
    Sha256();

    void update(const void* data, std::size_t size);
    void update(std::string_view text);
    std::array<std::uint8_t, 32> finish();

    static std::string hex(std::string_view text);
    static std::string hex(const void* data, std::size_t size);
    static std::string fileHex(const std::filesystem::path& path);

private:
    void transform(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t totalBytes_ = 0;
    std::size_t bufferSize_ = 0;
    bool finished_ = false;
};

std::string toHex(const std::array<std::uint8_t, 32>& digest);

}  // namespace clipfix
