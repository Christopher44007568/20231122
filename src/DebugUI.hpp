#pragma once

#include "Layout.hpp"
#include "PagingModel.hpp"
#include "Scenarios.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tpd {

struct DebugFrame {
    const PagingModel* model = nullptr;
    const Scenario* scenario = nullptr;
    size_t replayStep = 0;
    size_t replayTotal = 0;
    bool playing = true;
    bool fixedCamera = true;
    double speed = 1.0;
    double fps = 60.0;
};

class DebugUI {
public:
    struct Color {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };

    DebugUI();

    void build(const DebugFrame& frame, std::vector<uint8_t>& rgba);

private:
    std::vector<uint8_t> pixels_;

    void clear(Color color);
    void blendPixel(int x, int y, Color color);
    void fillRect(layout::Rect rect, Color color);
    void strokeRect(layout::Rect rect, Color color, int thickness = 1);
    void fillLine(int x0, int y0, int x1, int y1, Color color, int thickness = 1);
    void drawText(int x, int y, const std::string& text, Color color, int scale = 1);
    void drawTextRight(int right, int y, const std::string& text, Color color,
                       int scale = 1);

    void drawHeader(const DebugFrame& frame);
    void drawPageTable(const DebugFrame& frame);
    void drawSceneOverlay(const DebugFrame& frame);
    void drawUploadQueue(const DebugFrame& frame);
    void drawLayerTable(const DebugFrame& frame);
    void drawRequests(const DebugFrame& frame);
    void drawTrace(const DebugFrame& frame);
    void drawFooter(const DebugFrame& frame);
};

}  // namespace tpd
