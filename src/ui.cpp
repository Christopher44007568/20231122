#include "ui.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb/stb_easy_font.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cstring>

namespace ui_palette {
const UiColor Background{9, 13, 18, 255};
const UiColor Panel{17, 24, 32, 226};
const UiColor Text{224, 235, 241, 255};
const UiColor Muted{125, 149, 164, 255};
const UiColor Accent{30, 218, 198, 255};
const UiColor Warning{245, 190, 74, 255};
const UiColor Danger{246, 73, 87, 255};
const UiColor Success{64, 224, 133, 255};
}  // namespace ui_palette

UiBuilder::UiBuilder(int width, int height) {
  frame_.width = width;
  frame_.height = height;
}

void UiBuilder::triangle(float x0, float y0, float x1, float y1,
                         float x2, float y2, UiColor color) {
  frame_.vertices.push_back({x0, y0, 0.0f, color.r, color.g, color.b, color.a});
  frame_.vertices.push_back({x1, y1, 0.0f, color.r, color.g, color.b, color.a});
  frame_.vertices.push_back({x2, y2, 0.0f, color.r, color.g, color.b, color.a});
}

void UiBuilder::panel(int x, int y, int width, int height, UiColor color) {
  const float x0 = static_cast<float>(x);
  const float y0 = static_cast<float>(y);
  const float x1 = static_cast<float>(x + width);
  const float y1 = static_cast<float>(y + height);
  triangle(x0, y0, x1, y0, x1, y1, color);
  triangle(x0, y0, x1, y1, x0, y1, color);
}

void UiBuilder::outline(int x, int y, int width, int height, UiColor color,
                        int thickness) {
  panel(x, y, width, thickness, color);
  panel(x, y + height - thickness, width, thickness, color);
  panel(x, y, thickness, height, color);
  panel(x + width - thickness, y, thickness, height, color);
}

int UiBuilder::textWidth(const std::string& value, int scale) const {
  return stb_easy_font_width(const_cast<char*>(value.c_str())) * scale;
}

void UiBuilder::text(int x, int y, const std::string& value, UiColor color,
                     int scale) {
  if (value.empty()) {
    return;
  }
  std::vector<char> buffer(static_cast<std::size_t>(value.size()) * 256 + 256);
  const unsigned char textColor[4] = {color.r, color.g, color.b, color.a};
  const int quads = stb_easy_font_print(0.0f, 0.0f,
    const_cast<char*>(value.c_str()), const_cast<unsigned char*>(textColor),
    buffer.data(), static_cast<int>(buffer.size()));
  const auto* vertices = reinterpret_cast<const UiVertex*>(buffer.data());
  for (int quad = 0; quad < quads; ++quad) {
    UiVertex transformed[4];
    for (int corner = 0; corner < 4; ++corner) {
      UiVertex vertex = vertices[quad * 4 + corner];
      vertex.x = static_cast<float>(x) + vertex.x * static_cast<float>(scale);
      vertex.y = static_cast<float>(y) + vertex.y * static_cast<float>(scale);
      vertex.r = color.r;
      vertex.g = color.g;
      vertex.b = color.b;
      vertex.a = color.a;
      transformed[corner] = vertex;
    }
    frame_.vertices.push_back(transformed[0]);
    frame_.vertices.push_back(transformed[1]);
    frame_.vertices.push_back(transformed[2]);
    frame_.vertices.push_back(transformed[0]);
    frame_.vertices.push_back(transformed[2]);
    frame_.vertices.push_back(transformed[3]);
  }
}

void UiBuilder::marker(int x, int y, int radius, UiColor color) {
  panel(x - radius, y, radius * 2 + 1, 1, color);
  panel(x, y - radius, 1, radius * 2 + 1, color);
  outline(x - radius, y - radius, radius * 2 + 1, radius * 2 + 1, color, 1);
}

UiFrame UiBuilder::finish() { return std::move(frame_); }

