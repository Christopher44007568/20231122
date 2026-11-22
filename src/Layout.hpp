#pragma once

#include <cstdint>

namespace tpd::layout {

constexpr int kWidth = 1600;
constexpr int kHeight = 900;

struct Rect {
    int x;
    int y;
    int w;
    int h;

    bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

constexpr Rect kHeader{0, 0, kWidth, 64};
constexpr Rect kPagePanel{20, 76, 370, 400};
constexpr Rect kScenePanel{406, 76, 790, 504};
constexpr Rect kSceneFrame{418, 112, 760, 450};
constexpr Rect kQueuePanel{1212, 76, 368, 228};
constexpr Rect kLayerPanel{1212, 316, 368, 264};
constexpr Rect kRequestPanel{20, 590, 770, 290};
constexpr Rect kTracePanel{802, 590, 778, 290};

constexpr int kPageColumns = 4;
constexpr int kPageRows = 3;
constexpr int kTileWidth = kSceneFrame.w / kPageColumns;
constexpr int kTileHeight = kSceneFrame.h / kPageRows;

inline Rect tileRect(int pageId) {
    const int column = pageId % kPageColumns;
    const int row = pageId / kPageColumns;
    return {kSceneFrame.x + column * kTileWidth,
            kSceneFrame.y + row * kTileHeight, kTileWidth, kTileHeight};
}

}  // namespace tpd::layout
