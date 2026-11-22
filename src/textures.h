#pragma once
#include "image.h"
#include <string>

namespace tcpv {

// Deterministic procedural source textures, no external assets required.
// kind: "gradient" | "checker" | "noise" | "photo"
Image makeSourceTexture(const std::string& kind, uint32_t w, uint32_t h);

} // namespace tcpv
