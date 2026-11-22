#include "DebugUI.hpp"

#include "Font5x7.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace tpd {
namespace {

using Color = DebugUI::Color;

constexpr Color kHeader{11, 16, 23, 255};
constexpr Color kPanel{15, 21, 30, 246};
constexpr Color kPanelAlt{19, 27, 38, 246};
constexpr Color kBorder{45, 58, 74, 255};
constexpr Color kBorderSoft{32, 42, 56, 255};
constexpr Color kText{205, 216, 228, 255};
constexpr Color kDim{116, 130, 147, 255};
constexpr Color kFaint{73, 86, 101, 255};
constexpr Color kAccent{64, 210, 190, 255};
constexpr Color kBlue{88, 158, 255, 255};
constexpr Color kAmber{244, 181, 73, 255};
constexpr Color kMagenta{228, 111, 180, 255};
constexpr Color kRed{255, 107, 117, 255};
constexpr Color kGreen{93, 215, 139, 255};
constexpr Color kPurple{174, 137, 255, 255};
constexpr Color kFocus{38, 58, 71, 255};

std::string twoDigits(int value) {
    std::ostringstream out;
    out << std::setw(2) << std::setfill('0') << value;
    return out.str();
}

std::string layerName(int layer) {
    return layer < 0 ? "--" : "L" + twoDigits(layer);
}

std::string contentName(int content, bool duplicate = false) {
    if (content < 0) {
        return "EMPTY";
    }
    return "C" + twoDigits(content) + (duplicate ? "*" : "");
}

Color stateColor(PageState state) {
    switch (state) {
        case PageState::Resident: return kGreen;
        case PageState::Uploading: return kAmber;
        case PageState::Queued: return kBlue;
        case PageState::Empty: return kFaint;
        case PageState::Cold: return kDim;
    }
    return kText;
}

Color outcomeColor(RequestOutcome outcome) {
    switch (outcome) {
        case RequestOutcome::EmptyPage: return kFaint;
        case RequestOutcome::Hit: return kGreen;
        case RequestOutcome::DuplicateHit: return kMagenta;
        case RequestOutcome::MissQueued: return kAmber;
    }
    return kText;
}

int contentUseCount(const PagingModel& model, int contentId) {
    int count = 0;
    for (const PageEntry& page : model.pages()) {
        if (!page.empty && page.contentId == contentId) {
            ++count;
        }
    }
    return count;
}

const UploadItem* uploadForLayer(const PagingModel& model, int layerId) {
    for (auto it = model.uploads().rbegin(); it != model.uploads().rend(); ++it) {
        if (it->layer == layerId && it->state != UploadState::Complete) {
            return &*it;
        }
    }
    return nullptr;
}

}  // namespace

DebugUI::DebugUI() : pixels_(layout::kWidth * layout::kHeight * 4, 0) {}

void DebugUI::blendPixel(int x, int y, Color color) {
    if (x < 0 || y < 0 || x >= layout::kWidth || y >= layout::kHeight ||
        color.a == 0) {
        return;
    }
    const size_t offset =
        (static_cast<size_t>(y) * layout::kWidth + static_cast<size_t>(x)) * 4;
    if (color.a == 255) {
        pixels_[offset + 0] = color.r;
        pixels_[offset + 1] = color.g;
        pixels_[offset + 2] = color.b;
        pixels_[offset + 3] = color.a;
        return;
    }
    const uint32_t alpha = color.a;
    const uint32_t inverse = 255 - alpha;
    pixels_[offset + 0] = static_cast<uint8_t>(
        (color.r * alpha + pixels_[offset + 0] * inverse) / 255);
    pixels_[offset + 1] = static_cast<uint8_t>(
        (color.g * alpha + pixels_[offset + 1] * inverse) / 255);
    pixels_[offset + 2] = static_cast<uint8_t>(
        (color.b * alpha + pixels_[offset + 2] * inverse) / 255);
    pixels_[offset + 3] = static_cast<uint8_t>(
        alpha + (pixels_[offset + 3] * inverse) / 255);
}

void DebugUI::clear(Color color) {
    for (int y = 0; y < layout::kHeight; ++y) {
        for (int x = 0; x < layout::kWidth; ++x) {
            const size_t offset =
                (static_cast<size_t>(y) * layout::kWidth + static_cast<size_t>(x)) * 4;
            pixels_[offset + 0] = color.r;
            pixels_[offset + 1] = color.g;
            pixels_[offset + 2] = color.b;
            pixels_[offset + 3] = color.a;
        }
    }
}

void DebugUI::fillRect(layout::Rect rect, Color color) {
    const int x0 = std::max(0, rect.x);
    const int y0 = std::max(0, rect.y);
    const int x1 = std::min(layout::kWidth, rect.x + rect.w);
    const int y1 = std::min(layout::kHeight, rect.y + rect.h);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            blendPixel(x, y, color);
        }
    }
}

void DebugUI::strokeRect(layout::Rect rect, Color color, int thickness) {
    fillRect({rect.x, rect.y, rect.w, thickness}, color);
    fillRect({rect.x, rect.y + rect.h - thickness, rect.w, thickness}, color);
    fillRect({rect.x, rect.y, thickness, rect.h}, color);
    fillRect({rect.x + rect.w - thickness, rect.y, thickness, rect.h}, color);
}

void DebugUI::fillLine(int x0, int y0, int x1, int y1, Color color, int thickness) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        fillRect({x0, y0, thickness, thickness}, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void DebugUI::drawText(int x, int y, const std::string& text, Color color, int scale) {
    int cursor = x;
    for (char character : text) {
        const font::Glyph* glyph = font::findGlyph(character);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((glyph->rows[row] & (1u << (4 - column))) != 0) {
                    fillRect({cursor + column * scale, y + row * scale, scale, scale},
                             color);
                }
            }
        }
        cursor += 6 * scale;
    }
}

void DebugUI::drawTextRight(int right, int y, const std::string& text, Color color,
                            int scale) {
    drawText(right - static_cast<int>(text.size()) * 6 * scale, y, text, color,
             scale);
}

void DebugUI::build(const DebugFrame& frame, std::vector<uint8_t>& rgba) {
    clear({0, 0, 0, 0});
    drawHeader(frame);
    drawPageTable(frame);
    drawSceneOverlay(frame);
    drawUploadQueue(frame);
    drawLayerTable(frame);
    drawRequests(frame);
    drawTrace(frame);
    drawFooter(frame);
    rgba = pixels_;
}

void DebugUI::drawHeader(const DebugFrame& frame) {
    const PagingModel& model = *frame.model;
    fillRect(layout::kHeader, kHeader);
    fillRect({0, layout::kHeader.h - 2, layout::kWidth, 2}, kAccent);
    drawText(22, 14, "TEXTURE ARRAY PAGING DEBUGGER", kText, 3);
    drawText(24, 44, "C++17 / VULKAN / GLFW   ATOMIC MAPPING + LAYER BOUNDS",
             kDim, 1);

    const std::string scenario =
        frame.scenario ? frame.scenario->title : std::string("NO SCENARIO");
    const std::string camera = frame.fixedCamera ? "FIXED CAMERA" : "FREE CAMERA";
    const std::string playback = frame.playing ? "AUTO" : "PAUSED";

    drawTextRight(1574, 12,
                  "TICK " + std::to_string(model.currentTick()) + "   FPS " +
                      std::to_string(static_cast<int>(frame.fps + 0.5)),
                  kDim, 1);
    drawTextRight(1574, 28,
                  std::string(playback) + " " + std::to_string(frame.speed).substr(0, 3) +
                      "X",
                  kAccent, 1);
    drawTextRight(1574, 44,
                  "STEP " + std::to_string(frame.replayStep + 1) + "/" +
                      std::to_string(std::max<size_t>(1, frame.replayTotal)),
                  kDim, 1);

    const int scenarioWidth = static_cast<int>(scenario.size()) * 6 + 22;
    fillRect({742, 12, scenarioWidth, 18}, {25, 38, 49, 255});
    strokeRect({742, 12, scenarioWidth, 18}, kBorderSoft);
    drawText(752, 18, scenario, kBlue, 1);

    const int cameraWidth = static_cast<int>(camera.size()) * 6 + 22;
    fillRect({742, 34, cameraWidth, 18},
             frame.fixedCamera ? Color{22, 51, 49, 255} : Color{52, 35, 25, 255});
    strokeRect({742, 34, cameraWidth, 18},
               frame.fixedCamera ? kAccent : kAmber);
    drawText(752, 40, camera, frame.fixedCamera ? kAccent : kAmber, 1);

    const std::vector<std::string> invariantErrors = model.validateInvariants();
    const bool valid = invariantErrors.empty();
    drawText(742, 55, valid ? "INVARIANTS: OK" : "INVARIANTS: FAIL",
             valid ? kGreen : kRed, 1);
}

void DebugUI::drawPageTable(const DebugFrame& frame) {
    const PagingModel& model = *frame.model;
    const layout::Rect panel = layout::kPagePanel;
    fillRect(panel, kPanel);
    strokeRect(panel, kBorder);
    fillRect({panel.x, panel.y, panel.w, 3}, kBlue);
    drawText(panel.x + 14, panel.y + 12, "PAGE TABLE", kText, 1);
    drawText(panel.x + 112, panel.y + 12, "VIRTUAL -> ARRAY LAYER", kDim, 1);
    drawTextRight(panel.x + panel.w - 14, panel.y + 12,
                  "REQ " + std::to_string(model.requestCount()), kDim, 1);

    const int headerY = panel.y + 34;
    fillRect({panel.x + 8, headerY - 3, panel.w - 16, 17}, kPanelAlt);
    drawText(panel.x + 14, headerY, "PAGE", kFaint, 1);
    drawText(panel.x + 72, headerY, "CONTENT", kFaint, 1);
    drawText(panel.x + 150, headerY, "LAYER", kFaint, 1);
    drawText(panel.x + 206, headerY, "STATE", kFaint, 1);
    drawText(panel.x + 292, headerY, "HIT", kFaint, 1);
    drawText(panel.x + 330, headerY, "SEQ", kFaint, 1);

    const int rowStart = panel.y + 56;
    const int rowHeight = 27;
    for (const PageEntry& page : model.pages()) {
        const int y = rowStart + page.id * rowHeight;
        const bool focus = page.id == model.focusPage();
        if (focus) {
            fillRect({panel.x + 7, y - 4, panel.w - 14, rowHeight - 2}, kFocus);
            fillRect({panel.x + 7, y - 4, 3, rowHeight - 2}, kAccent);
        }
        const bool duplicate = !page.empty &&
                               contentUseCount(model, page.contentId) > 1;
        const Color pageColor = focus ? kText : (page.empty ? kFaint : kText);
        drawText(panel.x + 14, y, page.label, pageColor, 1);
        drawText(panel.x + 72, y, contentName(page.contentId, duplicate),
                 duplicate ? kMagenta : (page.empty ? kFaint : kDim), 1);
        const int shownLayer =
            page.state == PageState::Resident ? page.layer : page.reservedLayer;
        const std::string layer = layerName(shownLayer) +
                                  (page.state != PageState::Resident &&
                                           shownLayer >= 0
                                       ? "*"
                                       : "");
        drawText(panel.x + 150, y, layer,
                 page.state == PageState::Resident ? kGreen : kAmber, 1);
        drawText(panel.x + 206, y, toString(page.state), stateColor(page.state), 1);
        drawText(panel.x + 296, y, std::to_string(page.hitCount), kDim, 1);
        drawText(panel.x + 332, y,
                 page.lastRequestSeq == 0
                     ? "--"
                     : std::to_string(page.lastRequestSeq),
                 kFaint, 1);
    }
    drawText(panel.x + 14, panel.y + panel.h - 18,
             "* RESERVED LAYER / NOT YET SAMPLEABLE", kFaint, 1);
}

void DebugUI::drawSceneOverlay(const DebugFrame& frame) {
    const PagingModel& model = *frame.model;
    const layout::Rect panel = layout::kScenePanel;
    strokeRect(panel, kBorder);
    fillRect({panel.x, panel.y, panel.w, 3}, kAccent);
    drawText(panel.x + 14, panel.y + 12,
             "FIXED CAMERA // VIRTUAL PAGE GRID", kText, 1);
    drawTextRight(panel.x + panel.w - 14, panel.y + 12,
                  "YAW 0  PITCH 0  ZOOM 1.00", kDim, 1);

    strokeRect(layout::kSceneFrame, kBorderSoft);
    for (const PageEntry& page : model.pages()) {
        const layout::Rect tile = layout::tileRect(page.id);
        const bool focus = page.id == model.focusPage();
        const int sampleLayer = model.sampleLayer(page.id);

        fillRect({tile.x, tile.y, tile.w, 19}, {5, 8, 12, 188});
        drawText(tile.x + 7, tile.y + 6, page.label,
                 focus ? kAccent : kText, 1);
        const bool duplicate = !page.empty &&
                               contentUseCount(model, page.contentId) > 1;
        drawText(tile.x + 43, tile.y + 6,
                 contentName(page.contentId, duplicate),
                 duplicate ? kMagenta : kDim, 1);
        drawTextRight(tile.x + tile.w - 7, tile.y + 6,
                      sampleLayer >= 0 ? layerName(sampleLayer)
                                       : (page.empty ? "EMPTY" : "WAIT"),
                      sampleLayer >= 0 ? kGreen : (page.empty ? kFaint : kAmber), 1);

        if (page.empty) {
            const std::string message = "NO SOURCE DATA";
            const int width = static_cast<int>(message.size()) * 6;
            drawText(tile.x + (tile.w - width) / 2, tile.y + tile.h / 2 - 10,
                     message, kFaint, 1);
        } else if (sampleLayer < 0) {
            const std::string message =
                page.state == PageState::Cold ? "COLD / INVALID" : "UPLOAD WAIT";
            const int width = static_cast<int>(message.size()) * 6;
            drawText(tile.x + (tile.w - width) / 2, tile.y + tile.h / 2 - 10,
                     message, kAmber, 1);
        }

        strokeRect(tile, focus ? kAccent : kBorderSoft, focus ? 3 : 1);
        if (focus) {
            fillRect({tile.x + 8, tile.y + tile.h - 18, 54, 14},
                     {18, 52, 49, 220});
            drawText(tile.x + 13, tile.y + tile.h - 15, "SAMPLING", kAccent, 1);
        }
    }
}

void DebugUI::drawUploadQueue(const DebugFrame& frame) {
    const PagingModel& model = *frame.model;
    const layout::Rect panel = layout::kQueuePanel;
    fillRect(panel, kPanel);
    strokeRect(panel, kBorder);
    fillRect({panel.x, panel.y, panel.w, 3}, kAmber);
    drawText(panel.x + 14, panel.y + 12, "UPLOAD QUEUE", kText, 1);
    drawTextRight(panel.x + panel.w - 14, panel.y + 12,
                  "ACTIVE " + std::to_string(model.activeUploadCount()) + "/" +
                      std::to_string(model.config().uploadSlots),
                  kAmber, 1);

    const int headerY = panel.y + 34;
    fillRect({panel.x + 8, headerY - 3, panel.w - 16, 17}, kPanelAlt);
    drawText(panel.x + 14, headerY, "UPLOAD", kFaint, 1);
    drawText(panel.x + 78, headerY, "PAGE", kFaint, 1);
    drawText(panel.x + 132, headerY, "TARGET", kFaint, 1);
    drawText(panel.x + 194, headerY, "STATE", kFaint, 1);
    drawText(panel.x + 280, headerY, "READY IN", kFaint, 1);

    std::vector<const UploadItem*> visible;
    for (const UploadItem& item : model.uploads()) {
        if (item.state != UploadState::Complete) {
            visible.push_back(&item);
        }
    }
    for (auto it = model.uploads().rbegin();
         it != model.uploads().rend() && visible.size() < 7; ++it) {
        if (it->state == UploadState::Complete) {
            visible.push_back(&*it);
        }
    }
    std::sort(visible.begin(), visible.end(),
              [](const UploadItem* a, const UploadItem* b) {
                  if (a->state == b->state) {
                      return a->id < b->id;
                  }
                  return a->state != UploadState::Complete;
              });

    const int rowStart = panel.y + 56;
    if (visible.empty()) {
        drawText(panel.x + 14, rowStart + 8, "QUEUE EMPTY", kFaint, 1);
        return;
    }
    for (size_t index = 0; index < visible.size(); ++index) {
        const UploadItem& item = *visible[index];
        const int y = rowStart + static_cast<int>(index) * 22;
        if (item.state != UploadState::Complete) {
            fillRect({panel.x + 8, y - 4, panel.w - 16, 19},
                     {28, 35, 31, 180});
        }
        std::ostringstream id;
        id << "U" << std::setw(3) << std::setfill('0') << item.id;
        drawText(panel.x + 14, y, id.str(),
                 item.state == UploadState::Complete ? kFaint : kText, 1);
        drawText(panel.x + 78, y, "P" + twoDigits(item.pageId), kDim, 1);
        drawText(panel.x + 132, y, layerName(item.layer), kAmber, 1);
        drawText(panel.x + 194, y, toString(item.state),
                 item.state == UploadState::Complete ? kGreen : kAmber, 1);
        std::string ready;
        if (item.state == UploadState::Complete) {
            ready = "@" + std::to_string(item.readyTick);
        } else if (item.state == UploadState::Queued) {
            ready = "QUEUED";
        } else if (item.readyTick > model.currentTick()) {
            ready = "+" + std::to_string(item.readyTick - model.currentTick()) + "T";
        } else {
            ready = "DUE";
        }
        drawText(panel.x + 280, y, ready, kDim, 1);
    }
}

void DebugUI::drawLayerTable(const DebugFrame& frame) {
    const PagingModel& model = *frame.model;
    const layout::Rect panel = layout::kLayerPanel;
    fillRect(panel, kPanel);
    strokeRect(panel, kBorder);
    fillRect({panel.x, panel.y, panel.w, 3}, kGreen);
    drawText(panel.x + 14, panel.y + 12, "TEXTURE ARRAY LAYERS", kText, 1);
    drawTextRight(panel.x + panel.w - 14, panel.y + 12,
                  std::to_string(model.config().layerCount) + " BOUNDED SLOTS",
                  kDim, 1);

    const int headerY = panel.y + 34;
    fillRect({panel.x + 8, headerY - 3, panel.w - 16, 17}, kPanelAlt);
    drawText(panel.x + 14, headerY, "LAYER", kFaint, 1);
    drawText(panel.x + 68, headerY, "CONTENT", kFaint, 1);
    drawText(panel.x + 136, headerY, "OWNER", kFaint, 1);
    drawText(panel.x + 202, headerY, "STATE", kFaint, 1);
    drawText(panel.x + 276, headerY, "SHARED BY", kFaint, 1);

    int focusedLayer = kInvalidLayer;
    if (model.focusPage() >= 0) {
        focusedLayer = model.sampleLayer(model.focusPage());
    }
    const int rowStart = panel.y + 56;
    for (const LayerEntry& layer : model.layers()) {
        const int y = rowStart + layer.id * 25;
        if (layer.id == focusedLayer) {
            fillRect({panel.x + 7, y - 4, panel.w - 14, 21}, kFocus);
            fillRect({panel.x + 7, y - 4, 3, 21}, kAccent);
        }
        drawText(panel.x + 14, y, "L" + twoDigits(layer.id), kText, 1);
        drawText(panel.x + 68, y,
                 contentName(layer.ready ? layer.contentId : -1),
                 layer.ready ? kDim : kFaint, 1);
        drawText(panel.x + 136, y,
                 layer.primaryPage >= 0 ? "P" + twoDigits(layer.primaryPage) : "--",
                 kDim, 1);

        std::string state = "FREE";
        Color color = kFaint;
        const UploadItem* pending = uploadForLayer(model, layer.id);
        if (layer.ready) {
            state = "READY";
            color = kGreen;
        } else if (pending != nullptr) {
            state = pending->state == UploadState::Uploading ? "XFER" : "QUEUED";
            color = kAmber;
        }
        drawText(panel.x + 202, y, state, color, 1);

        int shared = 0;
        if (layer.ready) {
            for (const PageEntry& page : model.pages()) {
                if (page.state == PageState::Resident && page.layer == layer.id) {
                    ++shared;
                }
            }
        }
        drawText(panel.x + 276, y,
                 shared > 1 ? std::to_string(shared) + " PAGES" : "--",
                 shared > 1 ? kMagenta : kFaint, 1);
    }
}

void DebugUI::drawRequests(const DebugFrame& frame) {
    const PagingModel& model = *frame.model;
    const layout::Rect panel = layout::kRequestPanel;
    fillRect(panel, kPanel);
    strokeRect(panel, kBorder);
    fillRect({panel.x, panel.y, panel.w, 3}, kMagenta);
    drawText(panel.x + 14, panel.y + 12,
             "SAMPLING REQUESTS // PAGE-BY-PAGE", kText, 1);
    drawTextRight(panel.x + panel.w - 14, panel.y + 12,
                  "TOTAL " + std::to_string(model.traces().size()), kDim, 1);

    const int headerY = panel.y + 36;
    fillRect({panel.x + 8, headerY - 3, panel.w - 16, 17}, kPanelAlt);
    drawText(panel.x + 14, headerY, "SEQ", kFaint, 1);
    drawText(panel.x + 68, headerY, "PAGE", kFaint, 1);
    drawText(panel.x + 124, headerY, "SAMPLE", kFaint, 1);
    drawText(panel.x + 186, headerY, "CONTENT", kFaint, 1);
    drawText(panel.x + 252, headerY, "HIT", kFaint, 1);
    drawText(panel.x + 306, headerY, "FINAL", kFaint, 1);
    drawText(panel.x + 366, headerY, "WAIT", kFaint, 1);
    drawText(panel.x + 454, headerY, "OUTCOME", kFaint, 1);
    drawText(panel.x + 548, headerY, "REQ TICK", kFaint, 1);
    drawText(panel.x + 646, headerY, "READY TICK", kFaint, 1);

    const auto& traces = model.traces();
    const size_t visibleCount = std::min<size_t>(8, traces.size());
    const size_t first = traces.size() - visibleCount;
    for (size_t index = first; index < traces.size(); ++index) {
        const SwitchTrace& trace = traces[index];
        const int row = static_cast<int>(index - first);
        const int y = panel.y + 58 + row * 27;
        const bool focus = trace.pageId == model.focusPage();
        if (focus) {
            fillRect({panel.x + 7, y - 4, panel.w - 14, 21}, kFocus);
            fillRect({panel.x + 7, y - 4, 3, 21}, kAccent);
        }
        drawText(panel.x + 14, y, std::to_string(trace.seq),
                 trace.resolved ? kText : kAmber, 1);
        drawText(panel.x + 68, y, "P" + twoDigits(trace.pageId), kDim, 1);
        const int currentSample = model.sampleLayer(trace.pageId);
        drawText(panel.x + 124, y, layerName(currentSample),
                 currentSample >= 0 ? kGreen : kFaint, 1);
        const bool duplicate = !trace.empty &&
                               contentUseCount(model, trace.contentId) > 1;
        drawText(panel.x + 186, y, contentName(trace.contentId, duplicate),
                 trace.empty ? kFaint : (duplicate ? kMagenta : kDim), 1);
        drawText(panel.x + 252, y, layerName(trace.hitLayer),
                 trace.hitLayer >= 0 ? kGreen : kFaint, 1);
        drawText(panel.x + 306, y,
                 trace.empty ? "EMPTY" : layerName(trace.finalLayer),
                 trace.empty ? kFaint : kGreen, 1);
        const std::string wait =
            trace.resolved
                ? std::to_string(trace.waitTicks()) + "T"
                : "WAIT " + std::to_string(model.currentTick() - trace.requestTick) +
                      "T";
        drawText(panel.x + 366, y, wait, trace.resolved ? kDim : kAmber, 1);
        drawText(panel.x + 454, y, toString(trace.outcome),
                 outcomeColor(trace.outcome), 1);
        drawText(panel.x + 568, y, std::to_string(trace.requestTick), kFaint, 1);
        drawText(panel.x + 666, y,
                 trace.resolved ? std::to_string(trace.resolveTick) : "--", kFaint, 1);
    }
}

void DebugUI::drawTrace(const DebugFrame& frame) {
    const PagingModel& model = *frame.model;
    const layout::Rect panel = layout::kTracePanel;
    fillRect(panel, kPanel);
    strokeRect(panel, kBorder);
    fillRect({panel.x, panel.y, panel.w, 3}, kPurple);
    drawText(panel.x + 14, panel.y + 12,
             "PAGE SWITCH TRACE // REQUIRED OUTPUT", kText, 1);
    drawTextRight(panel.x + panel.w - 14, panel.y + 12,
                  "SEQ / WAIT / HIT / FINAL", kDim, 1);

    const int headerY = panel.y + 36;
    fillRect({panel.x + 8, headerY - 3, panel.w - 16, 17}, kPanelAlt);
    drawText(panel.x + 14, headerY, "SEQ", kFaint, 1);
    drawText(panel.x + 68, headerY, "PAGE", kFaint, 1);
    drawText(panel.x + 132, headerY, "WAIT", kFaint, 1);
    drawText(panel.x + 238, headerY, "HIT LAYER", kFaint, 1);
    drawText(panel.x + 332, headerY, "FINAL SAMPLE", kFaint, 1);
    drawText(panel.x + 448, headerY, "STATUS", kFaint, 1);
    drawText(panel.x + 540, headerY, "FLAGS", kFaint, 1);

    const auto& traces = model.traces();
    const size_t visibleCount = std::min<size_t>(8, traces.size());
    const size_t first = traces.size() - visibleCount;
    for (size_t index = first; index < traces.size(); ++index) {
        const SwitchTrace& trace = traces[index];
        const int row = static_cast<int>(index - first);
        const int y = panel.y + 58 + row * 27;
        const bool focus = trace.pageId == model.focusPage();
        if (focus) {
            fillRect({panel.x + 7, y - 4, panel.w - 14, 21}, kFocus);
            fillRect({panel.x + 7, y - 4, 3, 21}, kAccent);
        }
        drawText(panel.x + 14, y, std::to_string(trace.seq), kText, 1);
        drawText(panel.x + 68, y, "P" + twoDigits(trace.pageId), kDim, 1);
        std::ostringstream wait;
        if (trace.resolved) {
            wait << trace.waitTicks() << "T / " << std::fixed
                 << std::setprecision(1)
                 << trace.waitTicks() * model.config().frameMs << "MS";
        } else {
            wait << "PENDING";
        }
        drawText(panel.x + 132, y, wait.str(),
                 trace.resolved ? kDim : kAmber, 1);
        drawText(panel.x + 238, y, layerName(trace.hitLayer),
                 trace.hitLayer >= 0 ? kGreen : kFaint, 1);
        drawText(panel.x + 332, y,
                 trace.empty ? "EMPTY" : layerName(trace.finalLayer),
                 trace.empty ? kFaint : kGreen, 1);
        drawText(panel.x + 448, y, toString(trace.outcome),
                 outcomeColor(trace.outcome), 1);

        std::string flags;
        if (trace.empty) {
            flags += "EMPTY ";
        }
        if (trace.duplicate) {
            flags += "DUP ";
        }
        if (!trace.resolved) {
            flags += "PENDING";
        }
        if (flags.empty()) {
            flags = "ORDERED";
        }
        drawText(panel.x + 540, y, flags,
                 trace.empty ? kFaint : (trace.duplicate ? kMagenta : kDim), 1);
    }
}

void DebugUI::drawFooter(const DebugFrame& frame) {
    fillRect({0, 882, layout::kWidth, 18}, {7, 10, 14, 255});
    drawText(20, 889,
             "SPACE PLAY/PAUSE   LEFT/RIGHT STEP   1 EMPTY  2 DUP  3 CROSS  "
             "R RESET   +/- SPEED   ESC QUIT",
             kDim, 1);
    const std::vector<std::string> errors = frame.model->validateInvariants();
    drawTextRight(1580, 889,
                  errors.empty() ? "ATOMIC COMMIT: ON   LAYER CHECK: ON"
                                 : "INVARIANT ERROR",
                  errors.empty() ? kAccent : kRed, 1);
}

}  // namespace tpd
