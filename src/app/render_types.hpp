#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vfcr {

struct UiVertex {
    float x = 0.0F;
    float y = 0.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
};

struct CaptureResult {
    bool saved = false;
    std::string path;
    std::string sha256;
    int width = 0;
    int height = 0;
    std::string error;
};

}  // namespace vfcr
