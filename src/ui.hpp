#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct UiColor {
  std::uint8_t r = 255;
  std::uint8_t g = 255;
  std::uint8_t b = 255;
  std::uint8_t a = 255;
};

struct UiVertex {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  std::uint8_t r = 255;
  std::uint8_t g = 255;
  std::uint8_t b = 255;
  std::uint8_t a = 255;
};

struct UiFrame {
  int width = 0;
  int height = 0;
  std::vector<UiVertex> vertices;
};

class UiBuilder {
 public:
  UiBuilder(int width, int height);

  void panel(int x, int y, int width, int height, UiColor color);
  void outline(int x, int y, int width, int height, UiColor color,
               int thickness = 1);
  void text(int x, int y, const std::string& value, UiColor color,
            int scale = 1);
  int textWidth(const std::string& value, int scale = 1) const;
  void marker(int x, int y, int radius, UiColor color);

  UiFrame finish();

 private:
  void triangle(float x0, float y0, float x1, float y1,
                float x2, float y2, UiColor color);
  UiFrame frame_;
};

namespace ui_palette {
extern const UiColor Background;
extern const UiColor Panel;
extern const UiColor Text;
extern const UiColor Muted;
extern const UiColor Accent;
extern const UiColor Warning;
extern const UiColor Danger;
extern const UiColor Success;
}  // namespace ui_palette

