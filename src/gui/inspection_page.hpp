#pragma once

#include "../core/types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gcdump::gui {

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct Bitmap {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;

    Bitmap() = default;
    Bitmap(uint32_t w, uint32_t h)
        : width(w), height(h), pixels(static_cast<size_t>(w) * h * 4, 0) {}
};

// Renders one provenance report into an RGBA page. Layout deliberately keeps
// the same separation as the text report: automatic conclusions first, then
// manual confirmation, failures, boundary evidence and the raw source dump.
class InspectionPage {
public:
    static constexpr uint32_t kCharW = 5;
    static constexpr uint32_t kCharH = 7;
    static constexpr uint32_t kScale = 2;
    static constexpr uint32_t kPad = 16;

    explicit InspectionPage(Report report);

    const Bitmap& bitmap() const { return bitmap_; }
    std::array<float, 4> statusColor() const;

private:
    void drawText(std::string_view text, uint32_t x, uint32_t y, Rgba color,
                  uint32_t scale = kScale);
    void drawChar(char c, uint32_t x, uint32_t y, Rgba color, uint32_t scale);
    void fillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, Rgba color);
    uint32_t lineHeight() const { return (kCharH + 3) * kScale; }

    Report report_;
    Bitmap bitmap_;
};

} // namespace gcdump::gui
