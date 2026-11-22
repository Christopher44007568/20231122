#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crypto {

// RFC 6234 风格的流式 SHA-256，用于截图/字节证据的可复核指纹
class Sha256 {
public:
    Sha256();
    void update(const uint8_t* data, size_t length);
    std::vector<uint8_t> finish();

    static std::string hex(const std::vector<uint8_t>& digest);
    static std::string ofBytes(const uint8_t* data, size_t length);
    static std::string ofFile(const std::string& path);

private:
    uint32_t state_[8];
    uint64_t bitLength_ = 0;
    uint8_t buffer_[64];
    size_t bufferLen_ = 0;

    void processBlock(const uint8_t* block);
};

}  // namespace crypto
