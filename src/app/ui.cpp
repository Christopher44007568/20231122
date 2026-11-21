#include "app/ui.hpp"

#include "core/export.hpp"
#include "core/hash.hpp"
#include "core/io.hpp"
#include "core/session.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace vfcr {

namespace {

struct Glyph {
    char character;
    std::array<std::uint8_t, 5> columns;
};

constexpr Glyph kGlyphs[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x00, 0x00, 0x5f, 0x00, 0x00}},
    {'"', {0x00, 0x07, 0x00, 0x07, 0x00}},
    {'#', {0x14, 0x7f, 0x14, 0x7f, 0x14}},
    {'$', {0x24, 0x2a, 0x7f, 0x2a, 0x12}},
    {'%', {0x23, 0x13, 0x08, 0x64, 0x62}},
    {'&', {0x36, 0x49, 0x55, 0x22, 0x50}},
    {'\'', {0x00, 0x05, 0x03, 0x00, 0x00}},
    {'(', {0x00, 0x1c, 0x22, 0x41, 0x00}},
    {')', {0x00, 0x41, 0x22, 0x1c, 0x00}},
    {'*', {0x14, 0x08, 0x3e, 0x08, 0x14}},
    {'+', {0x08, 0x08, 0x3e, 0x08, 0x08}},
    {',', {0x00, 0x50, 0x30, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {'0', {0x3e, 0x51, 0x49, 0x45, 0x3e}},
    {'1', {0x00, 0x42, 0x7f, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4b, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7f, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3c, 0x4a, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1e}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {';', {0x00, 0x56, 0x36, 0x00, 0x00}},
    {'<', {0x08, 0x14, 0x22, 0x41, 0x00}},
    {'=', {0x14, 0x14, 0x14, 0x14, 0x14}},
    {'>', {0x00, 0x41, 0x22, 0x14, 0x08}},
    {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
    {'@', {0x32, 0x49, 0x79, 0x41, 0x3e}},
    {'A', {0x7e, 0x11, 0x11, 0x11, 0x7e}},
    {'B', {0x7f, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3e, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7f, 0x41, 0x41, 0x22, 0x1c}},
    {'E', {0x7f, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7f, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3e, 0x41, 0x49, 0x49, 0x7a}},
    {'H', {0x7f, 0x08, 0x08, 0x08, 0x7f}},
    {'I', {0x00, 0x41, 0x7f, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3f, 0x01}},
    {'K', {0x7f, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7f, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7f, 0x02, 0x0c, 0x02, 0x7f}},
    {'N', {0x7f, 0x04, 0x08, 0x10, 0x7f}},
    {'O', {0x3e, 0x41, 0x41, 0x41, 0x3e}},
    {'P', {0x7f, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3e, 0x41, 0x51, 0x21, 0x5e}},
    {'R', {0x7f, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7f, 0x01, 0x01}},
    {'U', {0x3f, 0x40, 0x40, 0x40, 0x3f}},
    {'V', {0x1f, 0x20, 0x40, 0x20, 0x1f}},
    {'W', {0x7f, 0x20, 0x18, 0x20, 0x7f}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {'[', {0x00, 0x7f, 0x41, 0x41, 0x00}},
    {'\\', {0x02, 0x04, 0x08, 0x10, 0x20}},
    {']', {0x00, 0x41, 0x41, 0x7f, 0x00}},
    {'^', {0x04, 0x02, 0x01, 0x02, 0x04}},
    {'_', {0x40, 0x40, 0x40, 0x40, 0x40}},
    {'`', {0x00, 0x01, 0x02, 0x04, 0x00}},
    {'{', {0x00, 0x08, 0x36, 0x41, 0x00}},
    {'|', {0x00, 0x00, 0x7f, 0x00, 0x00}},
    {'}', {0x00, 0x41, 0x36, 0x08, 0x00}},
    {'~', {0x08, 0x04, 0x08, 0x10, 0x08}},
};

const Glyph* glyphFor(char character) {
    if (character >= 'a' && character <= 'z') {
        character = static_cast<char>(character - 'a' + 'A');
    }
    for (const Glyph& glyph : kGlyphs) {
        if (glyph.character == character) {
            return &glyph;
        }
    }
    return &kGlyphs[0];
}

constexpr double kTimelineDuration = 30.0;

std::string clipText(std::string value, std::size_t maximum) {
    for (char& character : value) {
        if (character == '\n' || character == '\r') {
            character = ' ';
        }
    }
    if (value.size() > maximum) {
        value.resize(maximum > 2U ? maximum - 2U : maximum);
        value += "..";
    }
    return value;
}

}  // namespace

UiApp::UiApp(Session session, std::string statePath, std::string startupMessage)
    : session_(std::move(session)),
      statePath_(std::move(statePath)),
      startupMessage_(std::move(startupMessage)) {
    if (!startupMessage_.empty()) {
        setToast(startupMessage_);
    }
}

UiApp::Color UiApp::rgba(std::uint32_t hex, float alpha) {
    Color color;
    color.r = static_cast<float>((hex >> 16U) & 0xFFU) / 255.0F;
    color.g = static_cast<float>((hex >> 8U) & 0xFFU) / 255.0F;
    color.b = static_cast<float>(hex & 0xFFU) / 255.0F;
    color.a = alpha;
    return color;
}

bool UiApp::Rect::contains(double px, double py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
}

float UiApp::textWidth(const std::string& value, float scale) const {
    if (value.empty()) {
        return 0.0F;
    }
    return static_cast<float>(value.size() * 6U - 1U) * scale;
}

void UiApp::rect(float x, float y, float w, float h, Color color) {
    if (w <= 0.0F || h <= 0.0F) {
        return;
    }
    const auto add = [this, &color](float px, float py) {
        vertices_.push_back({px, py, color.r, color.g, color.b, color.a});
    };
    add(x, y);
    add(x + w, y);
    add(x + w, y + h);
    add(x, y);
    add(x + w, y + h);
    add(x, y + h);
}

void UiApp::outline(float x, float y, float w, float h, float thickness, Color color) {
    rect(x, y, w, thickness, color);
    rect(x, y + h - thickness, w, thickness, color);
    rect(x, y + thickness, thickness, h - thickness * 2.0F, color);
    rect(x + w - thickness, y + thickness, thickness, h - thickness * 2.0F, color);
}

void UiApp::triangle(float x1, float y1, float x2, float y2, float x3, float y3, Color color) {
    const auto add = [this, &color](float px, float py) {
        vertices_.push_back({px, py, color.r, color.g, color.b, color.a});
    };
    add(x1, y1);
    add(x2, y2);
    add(x3, y3);
}

void UiApp::polygon(const std::vector<float>& xy, Color color) {
    if (xy.size() < 6U) {
        return;
    }
    for (std::size_t i = 1; i + 1 < xy.size() / 2U; ++i) {
        triangle(xy[0], xy[1], xy[i * 2U], xy[i * 2U + 1U], xy[i * 2U + 2U],
                 xy[i * 2U + 3U], color);
    }
}

void UiApp::line(float x1, float y1, float x2, float y2, float thickness, Color color) {
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.001F) {
        return;
    }
    const float nx = -dy / length * thickness * 0.5F;
    const float ny = dx / length * thickness * 0.5F;
    const auto add = [this, &color](float px, float py) {
        vertices_.push_back({px, py, color.r, color.g, color.b, color.a});
    };
    add(x1 + nx, y1 + ny);
    add(x2 + nx, y2 + ny);
    add(x2 - nx, y2 - ny);
    add(x1 + nx, y1 + ny);
    add(x2 - nx, y2 - ny);
    add(x1 - nx, y1 - ny);
}

void UiApp::dashedLine(float x1, float y1, float x2, float y2, float thickness, float dash,
                       float gap, Color color) {
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.001F) {
        return;
    }
    const float ux = dx / length;
    const float uy = dy / length;
    for (float position = 0.0F; position < length; position += dash + gap) {
        const float end = std::min(position + dash, length);
        line(x1 + ux * position, y1 + uy * position, x1 + ux * end, y1 + uy * end, thickness,
             color);
    }
}

void UiApp::circle(float cx, float cy, float radius, int segments, Color color) {
    for (int i = 0; i < segments; ++i) {
        const float a0 = static_cast<float>(i) / static_cast<float>(segments) * 6.2831853F;
        const float a1 = static_cast<float>(i + 1) / static_cast<float>(segments) * 6.2831853F;
        triangle(cx, cy, cx + std::cos(a0) * radius, cy + std::sin(a0) * radius,
                 cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, color);
    }
}

void UiApp::ring(float cx, float cy, float radius, float thickness, int segments, Color color) {
    for (int i = 0; i < segments; ++i) {
        const float a0 = static_cast<float>(i) / static_cast<float>(segments) * 6.2831853F;
        const float a1 = static_cast<float>(i + 1) / static_cast<float>(segments) * 6.2831853F;
        const float inner = radius - thickness * 0.5F;
        const float outer = radius + thickness * 0.5F;
        const float c0 = std::cos(a0);
        const float s0 = std::sin(a0);
        const float c1 = std::cos(a1);
        const float s1 = std::sin(a1);
        const float x0 = cx + c0 * inner;
        const float y0 = cy + s0 * inner;
        const float x1 = cx + c1 * inner;
        const float y1 = cy + s1 * inner;
        const float x2 = cx + c1 * outer;
        const float y2 = cy + s1 * outer;
        const float x3 = cx + c0 * outer;
        const float y3 = cy + s0 * outer;
        triangle(x0, y0, x1, y1, x2, y2, color);
        triangle(x0, y0, x2, y2, x3, y3, color);
    }
}

void UiApp::text(float x, float y, const std::string& value, float scale, Color color) {
    float cursor = std::round(x);
    for (const char character : uppercase(value)) {
        const Glyph* glyph = glyphFor(character);
        for (int row = 0; row < 7; ++row) {
            int column = 0;
            while (column < 5) {
                const bool set = (glyph->columns[static_cast<std::size_t>(column)] &
                                  (1U << static_cast<unsigned>(row))) != 0U;
                if (!set) {
                    ++column;
                    continue;
                }
                int runEnd = column + 1;
                while (runEnd < 5) {
                    const bool nextSet =
                        (glyph->columns[static_cast<std::size_t>(runEnd)] &
                         (1U << static_cast<unsigned>(row))) != 0U;
                    if (!nextSet) {
                        break;
                    }
                    ++runEnd;
                }
                rect(cursor + static_cast<float>(column) * scale,
                     std::round(y + static_cast<float>(row) * scale),
                     static_cast<float>(runEnd - column) * scale, scale, color);
                column = runEnd;
            }
        }
        cursor += 6.0F * scale;
    }
}

void UiApp::textRight(float right, float y, const std::string& value, float scale, Color color) {
    text(right - textWidth(value, scale), y, value, scale, color);
}

void UiApp::textCentered(float cx, float y, const std::string& value, float scale, Color color) {
    text(cx - textWidth(value, scale) * 0.5F, y, value, scale, color);
}

std::string UiApp::uppercase(std::string value) {
    for (char& character : value) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return value;
}

std::string UiApp::frameTime(double seconds) {
    seconds = std::max(0.0, seconds);
    const int minutes = static_cast<int>(seconds) / 60;
    const double rest = seconds - static_cast<double>(minutes * 60);
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << minutes << ":" << std::setw(6)
        << std::fixed << std::setprecision(3) << rest;
    return out.str();
}

std::string UiApp::millisText(double value) {
    std::ostringstream out;
    out << (value >= 0.0 ? "+" : "") << std::fixed << std::setprecision(0) << value;
    return out.str();
}

UiApp::Layout UiApp::layoutFor(int width, int height) const {
    Layout layout;
    layout.width = static_cast<float>(width);
    layout.height = static_cast<float>(height);
    layout.leftW = std::max(250.0F, std::min(292.0F, layout.width * 0.225F));
    layout.rightW = std::max(224.0F, std::min(266.0F, layout.width * 0.198F));
    layout.centerX = layout.leftX + layout.leftW + layout.gap;
    layout.rightX = layout.width - layout.margin - layout.rightW;
    layout.centerW = layout.rightX - layout.gap - layout.centerX;
    layout.evidenceW = std::max(286.0F, std::min(330.0F, layout.width * 0.25F));
    layout.timelineW = layout.width - layout.margin * 2.0F - layout.evidenceW - layout.gap;
    layout.evidenceX = layout.width - layout.margin - layout.evidenceW;
    layout.bottomH = std::max(204.0F, std::min(236.0F, layout.height * 0.295F));
    layout.bottomY = layout.height - layout.margin - layout.bottomH;
    layout.mainH = layout.bottomY - layout.gap - layout.mainY;
    layout.mainH = std::max(330.0F, layout.mainH);
    return layout;
}

void UiApp::tick(double deltaSeconds) {
    pulse_ += deltaSeconds;
    if (session_.playing) {
        session_.playhead += deltaSeconds;
        if (session_.playhead >= kTimelineDuration) {
            session_.playhead = std::fmod(session_.playhead, kTimelineDuration);
        }
        markDirty();
    }
    if (toastTimer_ > 0.0) {
        toastTimer_ = std::max(0.0, toastTimer_ - deltaSeconds);
    }
    if (dirty_) {
        saveAccumulator_ += deltaSeconds;
        if (saveAccumulator_ >= 2.0) {
            std::string error;
            if (saveSessionFile(statePath_, session_, &error)) {
                dirty_ = false;
            } else {
                setToast("AUTOSAVE FAILED / " + error, true);
            }
            saveAccumulator_ = 0.0;
        }
    }
}

void UiApp::markDirty() {
    dirty_ = true;
    saveAccumulator_ = 0.0;
}

void UiApp::setToast(const std::string& message, bool error) {
    toast_ = uppercase(message);
    toastError_ = error;
    toastTimer_ = 5.0;
}

void UiApp::saveNow() {
    std::string error;
    if (saveSessionFile(statePath_, session_, &error)) {
        dirty_ = false;
        saveAccumulator_ = 0.0;
        setToast("SESSION SAVED / ATOMIC WRITE / " + shortDigest(evidenceHead(session_.evidence), 10));
    } else {
        setToast("SAVE FAILED / " + error, true);
    }
}

void UiApp::addHit(const Rect& rectValue, Action action, int index) {
    hits_.push_back({rectValue, action, index});
}

void UiApp::selectObject(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= session_.objects.size()) {
        return;
    }
    session_.selectedObject = index;
    session_.compareSequence = 0;
    markDirty();
}

void UiApp::rerunSelected() {
    if (session_.selectedObject < 0 ||
        static_cast<std::size_t>(session_.selectedObject) >= session_.objects.size()) {
        return;
    }
    const ReplayObject& object =
        session_.objects[static_cast<std::size_t>(session_.selectedObject)];
    const RunRecord run = rerunObject(session_, static_cast<std::size_t>(session_.selectedObject),
                                      session_.playhead, nowIso8601Utc());
    session_.compareSequence = run.sequence;
    markDirty();
    std::ostringstream message;
    message << "RERUN " << object.label << " / " << (run.visible ? "VISIBLE" : "CULLED")
            << " / " << std::fixed << std::setprecision(2) << run.costMs << "MS";
    setToast(message.str());
}

void UiApp::rerunAll() {
    vfcr::rerunAll(session_, session_.playhead, nowIso8601Utc());
    const ReplayObject& object =
        session_.objects[static_cast<std::size_t>(session_.selectedObject)];
    if (!object.runs.empty()) {
        session_.compareSequence = object.runs.back().sequence;
    }
    markDirty();
    setToast("ALL 6 OBJECTS REPLAYED / EVIDENCE APPENDED");
}

void UiApp::tweakOffset(double deltaMs) {
    adjustClockOffset(session_, deltaMs, nowIso8601Utc());
    markDirty();
    setToast("CLOCK OFFSET " + millisText(session_.clock.offsetMs) + "MS / EXISTING RUNS KEPT");
}

void UiApp::shiftGap(double deltaSeconds) {
    shiftMissingFragment(session_, deltaSeconds, nowIso8601Utc());
    markDirty();
    std::ostringstream message;
    message << "GAP " << session_.missing.id << " MOVED / [" << std::fixed
            << std::setprecision(2) << session_.missing.startA << " - "
            << session_.missing.endA << "]S";
    setToast(message.str());
}

void UiApp::cycleCompare() {
    if (session_.selectedObject < 0 ||
        static_cast<std::size_t>(session_.selectedObject) >= session_.objects.size()) {
        return;
    }
    const ReplayObject& object =
        session_.objects[static_cast<std::size_t>(session_.selectedObject)];
    const int maximum = static_cast<int>(object.runs.size());
    session_.compareSequence = (session_.compareSequence + 1) % (maximum + 1);
    markDirty();
}

void UiApp::queueExport(bool focusSelected) {
    if (pendingExport_) {
        return;
    }
    std::ostringstream directory;
    directory << "exports/" << compactTimestampUtc() << "-" << session_.frameId << "-"
              << std::setfill('0') << std::setw(2) << (session_.exportCount + 1);
    pendingFocusObjectId_.clear();
    if (focusSelected && session_.selectedObject >= 0 &&
        static_cast<std::size_t>(session_.selectedObject) < session_.objects.size()) {
        pendingFocusObjectId_ =
            session_.objects[static_cast<std::size_t>(session_.selectedObject)].id;
        directory << "-" << pendingFocusObjectId_;
    }
    pendingExportDirectory_ = directory.str();
    std::string error;
    if (!ensureDirectory(pendingExportDirectory_, &error)) {
        setToast("EXPORT DIRECTORY FAILED / " + error, true);
        pendingExportDirectory_.clear();
        return;
    }
    pendingExport_ = true;
    markDirty();
    setToast("RENDERING EVIDENCE CAPTURE...");
}

void UiApp::finishExport(const CaptureResult& capture) {
    if (snapshotOnly_) {
        snapshotOnly_ = false;
        snapshotDone_ = true;
        if (capture.saved) {
            setToast("FRAME SNAPSHOT / " + capture.path + " / " +
                     shortDigest(capture.sha256, 12));
        } else {
            setToast("FRAME SNAPSHOT FAILED / " + capture.error, true);
        }
        return;
    }
    if (!pendingExport_) {
        return;
    }
    ExportOptions options;
    options.directory = pendingExportDirectory_;
    options.screenshotFile = "frame.ppm";
    options.screenshotSha256 = capture.sha256;
    options.screenshotWidth = capture.width;
    options.screenshotHeight = capture.height;
    options.focusObjectId = pendingFocusObjectId_;
    std::string recordPath;
    std::string error;
    const bool wrote = writeExportBundle(session_, options, &recordPath, &error);
    pendingExport_ = false;
    pendingExportDirectory_.clear();
    pendingFocusObjectId_.clear();
    if (!wrote) {
        setToast("EXPORT FAILED / " + error, true);
        markDirty();
        return;
    }
    std::string saveError;
    if (saveSessionFile(statePath_, session_, &saveError)) {
        dirty_ = false;
    }
    if (capture.saved) {
        setToast("EXPORT VERIFIED / " + recordPath + " / " + shortDigest(capture.sha256, 10));
    } else {
        setToast("RECORD EXPORTED / CAPTURE FAILED / " + capture.error, true);
    }
}

void UiApp::pointerDown(double x, double y) {
    mouseX_ = x;
    mouseY_ = y;
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (!it->rect.contains(x, y)) {
            continue;
        }
        if (it->action == Action::Seek) {
            const double t = std::clamp((x - seekRegion_.x) / std::max(1.0F, seekRegion_.w), 0.0,
                                        static_cast<double>(kTimelineDuration));
            session_.playhead = t;
            draggingTimeline_ = true;
            markDirty();
        } else {
            trigger(it->action, it->index, x, y);
        }
        return;
    }
}

void UiApp::pointerMove(double x, double y) {
    mouseX_ = x;
    mouseY_ = y;
    if (draggingTimeline_) {
        const double t = std::clamp((x - seekRegion_.x) / std::max(1.0F, seekRegion_.w), 0.0,
                                    static_cast<double>(kTimelineDuration));
        session_.playhead = t;
        session_.playing = false;
        markDirty();
    }
}

void UiApp::pointerUp() {
    draggingTimeline_ = false;
}

void UiApp::trigger(Action action, int index, double x, double y) {
    (void)x;
    (void)y;
    switch (action) {
        case Action::SelectObject:
            selectObject(index);
            break;
        case Action::TogglePlay:
            session_.playing = !session_.playing;
            markDirty();
            break;
        case Action::RerunSelected:
            rerunSelected();
            break;
        case Action::RerunAll:
            rerunAll();
            break;
        case Action::OffsetDown:
            tweakOffset(-10.0);
            break;
        case Action::OffsetUp:
            tweakOffset(10.0);
            break;
        case Action::GapEarlier:
            shiftGap(-0.1);
            break;
        case Action::GapLater:
            shiftGap(0.1);
            break;
        case Action::SelectRun:
            session_.compareSequence = index;
            markDirty();
            break;
        case Action::Save:
            saveNow();
            break;
        case Action::Export:
            queueExport(false);
            break;
        case Action::ExportObject:
            queueExport(true);
            break;
        case Action::CycleCompare:
            cycleCompare();
            break;
        case Action::Seek:
        case Action::None:
            break;
    }
}

void UiApp::keyDown(int key, int mods) {
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    switch (key) {
        case GLFW_KEY_ESCAPE:
            saveNow();
            exitRequested_ = true;
            break;
        case GLFW_KEY_SPACE:
            session_.playing = !session_.playing;
            markDirty();
            break;
        case GLFW_KEY_R:
            rerunSelected();
            break;
        case GLFW_KEY_A:
            rerunAll();
            break;
        case GLFW_KEY_C:
            cycleCompare();
            break;
        case GLFW_KEY_E:
            queueExport(shift);
            break;
        case GLFW_KEY_LEFT_BRACKET:
            shiftGap(-0.1);
            break;
        case GLFW_KEY_RIGHT_BRACKET:
            shiftGap(0.1);
            break;
        case GLFW_KEY_S:
            saveNow();
            break;
        case GLFW_KEY_LEFT:
            session_.playhead =
                std::max(0.0, session_.playhead - (shift ? 0.5 : 0.1));
            session_.playing = false;
            markDirty();
            break;
        case GLFW_KEY_RIGHT:
            session_.playhead =
                std::min(kTimelineDuration, session_.playhead + (shift ? 0.5 : 0.1));
            session_.playing = false;
            markDirty();
            break;
        case GLFW_KEY_UP:
            selectObject(session_.selectedObject - 1);
            break;
        case GLFW_KEY_DOWN:
            selectObject(session_.selectedObject + 1);
            break;
        default:
            break;
    }
}

void UiApp::scroll(double offset) {
    if (std::abs(offset) < 0.01) {
        return;
    }
    tweakOffset(offset > 0.0 ? 10.0 : -10.0);
}

UiFrame UiApp::buildFrame(int width, int height) {
    vertices_.clear();
    hits_.clear();
    const Layout layout = layoutFor(width, height);
    drawChrome(layout);
    drawTopBar(layout);
    drawObjectList(layout);
    drawFrustum(layout);
    drawCorrectionPanel(layout);
    drawTimeline(layout);
    drawEvidence(layout);
    drawToast(layout);

    UiFrame frame;
    frame.vertices = vertices_;
    if (snapshotOnly_) {
        frame.capture = true;
        frame.capturePath = snapshotPath_;
    } else if (pendingExport_) {
        frame.capture = true;
        frame.capturePath = pathJoin(pendingExportDirectory_, "frame.ppm");
        frame.exportDirectory = pendingExportDirectory_;
    }
    return frame;
}

void UiApp::requestSnapshot(std::string path) {
    snapshotPath_ = std::move(path);
    snapshotOnly_ = true;
    snapshotDone_ = false;
    std::string error;
    if (!ensureDirectory(parentPath(snapshotPath_), &error)) {
        setToast("SNAPSHOT DIRECTORY FAILED / " + error, true);
        snapshotOnly_ = false;
        snapshotDone_ = true;
        return;
    }
    setToast("CAPTURING VULKAN FRAME...");
}

void UiApp::drawChrome(const Layout& layout) {
    const Color background = rgba(0x080D13);
    const Color panel = rgba(0x101821);
    const Color border = rgba(0x243543);
    rect(0.0F, 0.0F, layout.width, layout.height, background);
    rect(0.0F, 0.0F, layout.width, layout.top, panel);
    rect(0.0F, layout.top - 1.0F, layout.width, 1.0F, border);
    rect(0.0F, layout.top + 1.0F, 3.0F, layout.height - layout.top, rgba(0x57E0D2, 0.55F));
    for (int i = 0; i < 5; ++i) {
        const float alpha = 0.05F - static_cast<float>(i) * 0.008F;
        rect(0.0F, layout.height - 110.0F + static_cast<float>(i) * 22.0F, layout.width, 1.0F,
             rgba(0x57E0D2, std::max(0.0F, alpha)));
    }
}

void UiApp::drawTopBar(const Layout& layout) {
    const Color textColor = rgba(0xE8F2F8);
    const Color muted = rgba(0x728797);
    const Color cyan = rgba(0x57E0D2);
    const Color border = rgba(0x2A3D4C);
    const Color buttonFill = rgba(0x16232E);
    const float titleX = 22.0F;
    rect(14.0F, 15.0F, 5.0F, 34.0F, cyan);
    rect(22.0F, 15.0F, 3.0F, 20.0F, rgba(0x57E0D2, 0.4F));
    text(titleX + 8.0F, 13.0F, "VFC // FRUSTUM REPLAY", 3.0F, textColor);
    text(titleX + 10.0F, 42.0F, "LOCAL CORRECTION WORKBENCH", 1.2F, muted);
    text(titleX + 238.0F, 42.0F, "C++17", 1.2F, cyan);
    text(titleX + 278.0F, 42.0F, "/ VULKAN / GLFW", 1.2F, rgba(0x8EA4B3));

    struct ButtonSpec {
        const char* label;
        Action action;
        float width;
    };
    const ButtonSpec specs[] = {
        {"SAVE", Action::Save, 62.0F},
        {"EXPORT", Action::Export, 82.0F},
        {"RERUN", Action::RerunSelected, 76.0F},
    };
    float x = layout.width - 16.0F;
    for (int i = 2; i >= 0; --i) {
        x -= specs[i].width;
        const Rect box{x, 16.0F, specs[i].width, 32.0F};
        const bool primary = specs[i].action == Action::RerunSelected;
        rect(box.x, box.y, box.w, box.h, primary ? rgba(0x173D3E) : buttonFill);
        outline(box.x, box.y, box.w, box.h, 1.0F,
                primary ? rgba(0x57E0D2, 0.75F) : border);
        textCentered(box.x + box.w * 0.5F, box.y + 9.0F, specs[i].label, 1.5F,
                     primary ? cyan : textColor);
        addHit(box, specs[i].action);
        x -= 8.0F;
    }

    const bool insideGap = session_.playhead >= session_.missing.startA &&
                           session_.playhead <= session_.missing.endA;
    const std::string state = insideGap ? "MISSING SEGMENT ACTIVE" : "SYNC WINDOW STABLE";
    const Color stateColor = insideGap ? rgba(0xF1B84B) : rgba(0x63D6A0);
    const float pillWidth = textWidth(state, 1.2F) + 26.0F;
    const float pillX = std::max(470.0F, x - pillWidth - 10.0F);
    rect(pillX, 21.0F, pillWidth, 22.0F, rgba(insideGap ? 0x392C16 : 0x153127, 0.7F));
    outline(pillX, 21.0F, pillWidth, 22.0F, 1.0F, rgba(insideGap ? 0xF1B84B : 0x63D6A0, 0.5F));
    circle(pillX + 11.0F, 32.0F, 3.0F, 12, stateColor);
    text(pillX + 20.0F, 27.0F, state, 1.2F, stateColor);
}

void UiApp::drawObjectList(const Layout& layout) {
    const float x = layout.leftX;
    const float y = layout.mainY;
    const float w = layout.leftW;
    const float h = layout.mainH;
    const Color panel = rgba(0x101821);
    const Color panelAlt = rgba(0x0D151D);
    const Color border = rgba(0x243543);
    const Color textColor = rgba(0xE8F2F8);
    const Color muted = rgba(0x728797);
    const Color cyan = rgba(0x57E0D2);
    const Color green = rgba(0x63D6A0);
    const Color red = rgba(0xF06A6A);
    const Color amber = rgba(0xF1B84B);

    rect(x, y, w, h, panel);
    outline(x, y, w, h, 1.0F, border);
    rect(x, y, 3.0F, 36.0F, cyan);
    text(x + 16.0F, y + 13.0F, "OBJECT REGISTRY", 1.8F, textColor);
    textRight(x + w - 14.0F, y + 15.0F, "06 OBJ", 1.0F, muted);
    rect(x + 12.0F, y + 40.0F, w - 24.0F, 1.0F, border);

    const float listTop = y + 48.0F;
    const float listHeight = h - 58.0F;
    const float rowHeight = std::min(64.0F, listHeight / 6.0F);
    for (std::size_t i = 0; i < session_.objects.size(); ++i) {
        const ReplayObject& object = session_.objects[i];
        const Rect row{x + 6.0F, listTop + static_cast<float>(i) * rowHeight, w - 12.0F,
                       rowHeight - 3.0F};
        const bool selected = static_cast<int>(i) == session_.selectedObject;
        rect(row.x, row.y, row.w, row.h, selected ? rgba(0x172B35) : panelAlt);
        if (selected) {
            rect(row.x, row.y, 3.0F, row.h, cyan);
            outline(row.x, row.y, row.w, row.h, 1.0F, rgba(0x57E0D2, 0.5F));
        }
        const bool hasRun = !object.runs.empty();
        const bool stateVisible = hasRun ? object.runs.back().visible : object.baselineVisible;
        Color stateColor = stateVisible ? green : red;
        if (!object.baselineKnown && !hasRun) {
            stateColor = amber;
        }
        if (!hasRun) {
            rect(row.x + 14.0F, row.y + 17.0F, 10.0F, 10.0F, rgba(0x0A1117));
            outline(row.x + 14.0F, row.y + 17.0F, 10.0F, 10.0F, 1.0F, stateColor);
        } else {
            rect(row.x + 12.0F, row.y + 16.0F, 14.0F, 12.0F, rgba(0x0A1117));
            rect(row.x + 14.0F, row.y + 19.0F, 10.0F, 6.0F, stateColor);
        }
        text(row.x + 36.0F, row.y + 10.0F, object.label, 1.7F, textColor);
        text(row.x + 36.0F, row.y + 31.0F, object.id + " / " + object.kind, 1.0F, muted);
        textRight(row.x + row.w - 12.0F, row.y + 10.0F,
                  hasRun ? (stateVisible ? "VISIBLE" : "CULLED")
                         : (object.baselineKnown ? (object.baselineVisible ? "BASE VIS" : "BASE CULL")
                                                 : "MISSING"),
                  1.0F, stateColor);
        std::ostringstream meta;
        meta << "TRI " << object.triangles << " / RUN " << object.runs.size();
        textRight(row.x + row.w - 12.0F, row.y + 31.0F, meta.str(), 0.9F,
                  hasRun ? rgba(0x9BB0BE) : muted);
        if (!object.baselineKnown) {
            const float chipW = 34.0F;
            rect(row.x + row.w - chipW - 10.0F, row.y + row.h - 16.0F, chipW, 11.0F,
                 rgba(0xF1B84B, 0.14F));
            textCentered(row.x + row.w - chipW * 0.5F - 10.0F, row.y + row.h - 14.0F, "GAP",
                         0.8F, amber);
        }
        addHit(row, Action::SelectObject, static_cast<int>(i));
    }
}

void UiApp::drawFrustum(const Layout& layout) {
    const float x = layout.centerX;
    const float y = layout.mainY;
    const float w = layout.centerW;
    const float h = layout.mainH;
    const Color panel = rgba(0x0D151D);
    const Color border = rgba(0x243543);
    const Color textColor = rgba(0xE8F2F8);
    const Color muted = rgba(0x728797);
    const Color cyan = rgba(0x57E0D2);
    const Color green = rgba(0x63D6A0);
    const Color red = rgba(0xF06A6A);
    const Color amber = rgba(0xF1B84B);
    const Color grid = rgba(0x1A2A36);

    rect(x, y, w, h, panel);
    outline(x, y, w, h, 1.0F, border);
    text(x + 14.0F, y + 13.0F, "FRUSTUM / TOP-DOWN REPLAY", 1.7F, textColor);
    textRight(x + w - 14.0F, y + 14.0F, "CAM-A / FRAME 18422", 1.0F, muted);
    const Rect viewport{x + 12.0F, y + 38.0F, w - 24.0F, h - 78.0F};
    rect(viewport.x, viewport.y, viewport.w, viewport.h, rgba(0x081017));
    outline(viewport.x, viewport.y, viewport.w, viewport.h, 1.0F, rgba(0x1F3240));

    for (float gx = viewport.x + 20.0F; gx < viewport.x + viewport.w; gx += 24.0F) {
        line(gx, viewport.y, gx, viewport.y + viewport.h, 1.0F, grid);
    }
    for (float gy = viewport.y + 20.0F; gy < viewport.y + viewport.h; gy += 24.0F) {
        line(viewport.x, gy, viewport.x + viewport.w, gy, 1.0F, grid);
    }

    const float cameraX = viewport.x + viewport.w * 0.5F;
    const float cameraY = viewport.y + viewport.h - 24.0F;
    const float farY = viewport.y + 20.0F;
    const float topWidth = viewport.w * 0.57F;
    const float leftFar = std::max(viewport.x + 1.0F, cameraX - topWidth);
    const float rightFar = std::min(viewport.x + viewport.w - 1.0F, cameraX + topWidth);
    polygon({cameraX, cameraY, leftFar, farY, rightFar, farY}, rgba(0x57E0D2, 0.055F));
    dashedLine(cameraX, cameraY, leftFar, farY, 1.0F, 5.0F, 5.0F, rgba(0x57E0D2, 0.35F));
    dashedLine(cameraX, cameraY, rightFar, farY, 1.0F, 5.0F, 5.0F, rgba(0x57E0D2, 0.35F));
    line(leftFar, farY, rightFar, farY, 1.0F, rgba(0x57E0D2, 0.42F));
    for (int range = 10; range <= 30; range += 10) {
        const float fraction = static_cast<float>(range) / 42.0F;
        const float ry = cameraY + (farY - cameraY) * fraction;
        const float half = topWidth * fraction;
        dashedLine(std::max(viewport.x + 2.0F, cameraX - half), ry,
                   std::min(viewport.x + viewport.w - 2.0F, cameraX + half), ry, 1.0F, 2.0F,
                   7.0F, rgba(0x8097A7, 0.22F));
        text(std::min(viewport.x + viewport.w - 28.0F, cameraX + half + 4.0F), ry - 6.0F,
             std::to_string(range) + "M", 0.8F, rgba(0x728797, 0.75F));
    }
    line(viewport.x, cameraY, viewport.x + viewport.w, cameraY, 1.0F, rgba(0x8097A7, 0.25F));
    line(cameraX, viewport.y, cameraX, viewport.y + viewport.h, 1.0F, rgba(0x8097A7, 0.12F));
    circle(cameraX, cameraY, 7.0F, 20, rgba(0x57E0D2, 0.19F));
    ring(cameraX, cameraY, 7.0F, 1.5F, 20, cyan);
    triangle(cameraX - 4.0F, cameraY - 4.0F, cameraX + 4.0F, cameraY - 4.0F, cameraX,
             cameraY + 5.0F, cyan);
    text(cameraX + 12.0F, cameraY - 5.0F, "CAM-A", 1.0F, cyan);

    const auto screenPoint = [&](double wx, double wz) {
        const float sx = cameraX + static_cast<float>(wx / 34.0) * viewport.w * 0.45F;
        const float sy = cameraY + (farY - cameraY) * (static_cast<float>(wz) / 42.0F);
        return std::pair<float, float>{sx, sy};
    };

    for (std::size_t i = 0; i < session_.objects.size(); ++i) {
        const ReplayObject& object = session_.objects[i];
        const Vec3 position = objectPositionAt(object, session_.playhead);
        const CullEvaluation evaluation =
            evaluateCulling(session_.camera, object, session_.playhead);
        const auto [sx, sy] = screenPoint(position.x, position.z);
        if (sx < viewport.x - 40.0F || sx > viewport.x + viewport.w + 40.0F ||
            sy < viewport.y - 30.0F || sy > viewport.y + viewport.h + 30.0F) {
            continue;
        }
        const bool selected = static_cast<int>(i) == session_.selectedObject;
        Color color = evaluation.visible ? green : red;
        const bool inGap = session_.playhead >= session_.missing.startA &&
                           session_.playhead <= session_.missing.endA;
        if (!object.baselineKnown && inGap) {
            color = amber;
        }
        if (selected) {
            const float pulse = 7.0F + static_cast<float>(std::sin(pulse_ * 4.0)) * 1.5F;
            ring(sx, sy, 14.0F + pulse, 1.0F, 24, rgba(0x57E0D2, 0.45F));
            ring(sx, sy, 21.0F + pulse * 0.5F, 1.0F, 24, rgba(0x57E0D2, 0.16F));
        }
        if (object.kind == "STRUCTURE") {
            rect(sx - 6.0F, sy - 6.0F, 12.0F, 12.0F, rgba(0x081017));
            outline(sx - 6.0F, sy - 6.0F, 12.0F, 12.0F, 2.0F, color);
        } else if (object.kind == "DYNAMIC") {
            polygon({sx, sy - 8.0F, sx + 7.0F, sy, sx, sy + 8.0F, sx - 7.0F, sy}, color);
        } else if (object.kind == "SIGNAL") {
            triangle(sx, sy - 8.0F, sx + 7.0F, sy + 6.0F, sx - 7.0F, sy + 6.0F, color);
            ring(sx, sy, 11.0F, 1.0F, 20, Color{color.r, color.g, color.b, 0.3F});
        } else {
            circle(sx, sy, 6.0F, 16, rgba(0x081017));
            ring(sx, sy, 6.0F, 2.0F, 16, color);
        }
        text(sx + 9.0F, sy - 12.0F, object.label, 0.9F,
             selected ? textColor : rgba(0xC2D2DC, 0.82F));
        if (!object.baselineKnown) {
            text(sx + 9.0F, sy + 6.0F, "GAP", 0.8F, amber);
        } else if (!object.runs.empty()) {
            const RunRecord& run = object.runs.back();
            if (run.changed) {
                text(sx + 9.0F, sy + 6.0F, "CHANGED", 0.8F, amber);
            }
        }
    }

    text(viewport.x + 10.0F, viewport.y + 8.0F, "FOV 56 / NEAR 2.0 / FAR 42.0", 0.9F,
         rgba(0x8EA4B3, 0.8F));
    text(viewport.x + 10.0F, viewport.y + viewport.h - 16.0F,
         "PLAYHEAD " + frameTime(session_.playhead) + " / X-AXIS +-34M", 0.9F,
         rgba(0x8EA4B3, 0.8F));
    textRight(viewport.x + viewport.w - 10.0F, viewport.y + viewport.h - 16.0F,
              "GREEN VISIBLE / RED CULLED / AMBER GAP", 0.9F, rgba(0x8EA4B3, 0.78F));
    text(x + 14.0F, y + h - 25.0F, "PLANES", 0.9F, muted);
    text(x + 64.0F, y + h - 25.0F, "L R T B N F", 0.9F, cyan);
    textRight(x + w - 14.0F, y + h - 25.0F,
              std::to_string(static_cast<int>(session_.playhead * 10.0)) + " / 300 FRAME",
              0.9F, muted);
}

void UiApp::drawCorrectionPanel(const Layout& layout) {
    const float x = layout.rightX;
    const float y = layout.mainY;
    const float w = layout.rightW;
    const float h = layout.mainH;
    const Color panel = rgba(0x101821);
    const Color card = rgba(0x0D151D);
    const Color border = rgba(0x243543);
    const Color textColor = rgba(0xE8F2F8);
    const Color muted = rgba(0x728797);
    const Color cyan = rgba(0x57E0D2);
    const Color green = rgba(0x63D6A0);
    const Color red = rgba(0xF06A6A);
    const Color amber = rgba(0xF1B84B);

    rect(x, y, w, h, panel);
    outline(x, y, w, h, 1.0F, border);
    rect(x + w - 3.0F, y, 3.0F, 36.0F, amber);
    text(x + 14.0F, y + 13.0F, "LOCAL CORRECTION", 1.6F, textColor);
    textRight(x + w - 14.0F, y + 15.0F, "RUNS", 0.9F, muted);

    const float clockY = y + 42.0F;
    rect(x + 10.0F, clockY, w - 20.0F, 78.0F, card);
    outline(x + 10.0F, clockY, w - 20.0F, 78.0F, 1.0F, rgba(0x243543));
    text(x + 18.0F, clockY + 10.0F, "CLOCK SYNC / B-A", 0.9F, muted);
    text(x + 18.0F, clockY + 20.0F, millisText(session_.clock.offsetMs), 2.7F,
         session_.clock.offsetMs == 0.0 ? green : amber);
    text(x + 88.0F, clockY + 33.0F, "MS", 0.9F, muted);
    text(x + 18.0F, clockY + 47.0F, clipText(session_.clock.method, 23), 0.72F,
         rgba(0x9CB0BE));
    const Rect minus{x + w - 78.0F, clockY + 10.0F, 28.0F, 22.0F};
    const Rect plus{x + w - 44.0F, clockY + 10.0F, 28.0F, 22.0F};
    rect(minus.x, minus.y, minus.w, minus.h, rgba(0x1A2631));
    rect(plus.x, plus.y, plus.w, plus.h, rgba(0x1A2631));
    outline(minus.x, minus.y, minus.w, minus.h, 1.0F, border);
    outline(plus.x, plus.y, plus.w, plus.h, 1.0F, border);
    textCentered(minus.x + minus.w * 0.5F, minus.y + 6.0F, "-10", 1.0F, textColor);
    textCentered(plus.x + plus.w * 0.5F, plus.y + 6.0F, "+10", 1.0F, textColor);
    addHit(minus, Action::OffsetDown);
    addHit(plus, Action::OffsetUp);
    text(x + 18.0F, clockY + 63.0F, session_.clock.deviceA, 0.78F, green);
    textRight(x + w - 18.0F, clockY + 63.0F, session_.clock.deviceB, 0.78F, cyan);

    const float missingY = clockY + 82.0F;
    rect(x + 10.0F, missingY, w - 20.0F, 52.0F, rgba(0x382B14, 0.72F));
    outline(x + 10.0F, missingY, w - 20.0F, 52.0F, 1.0F, rgba(0xF1B84B, 0.55F));
    rect(x + 10.0F, missingY, 3.0F, 52.0F, amber);
    text(x + 20.0F, missingY + 9.0F, "MISSING FRAGMENT", 1.0F, amber);
    const Rect gapEarlier{x + w - 52.0F, missingY + 7.0F, 20.0F, 17.0F};
    const Rect gapLater{x + w - 28.0F, missingY + 7.0F, 20.0F, 17.0F};
    rect(gapEarlier.x, gapEarlier.y, gapEarlier.w, gapEarlier.h, rgba(0x2B2111));
    rect(gapLater.x, gapLater.y, gapLater.w, gapLater.h, rgba(0x2B2111));
    outline(gapEarlier.x, gapEarlier.y, gapEarlier.w, gapEarlier.h, 1.0F,
            rgba(0xF1B84B, 0.45F));
    outline(gapLater.x, gapLater.y, gapLater.w, gapLater.h, 1.0F,
            rgba(0xF1B84B, 0.45F));
    textCentered(gapEarlier.x + 10.0F, gapEarlier.y + 5.0F, "<", 0.8F, amber);
    textCentered(gapLater.x + 10.0F, gapLater.y + 5.0F, ">", 0.8F, amber);
    addHit(gapEarlier, Action::GapEarlier);
    addHit(gapLater, Action::GapLater);
    std::ostringstream window;
    window << "[" << std::fixed << std::setprecision(3) << session_.missing.startA << " - "
           << session_.missing.endA << "]S / "
           << std::setprecision(2) << (session_.missing.endA - session_.missing.startA) << "S";
    text(x + 20.0F, missingY + 24.0F, window.str(), 1.0F, textColor);
    std::ostringstream rawB;
    rawB << "RAW B +" << std::fixed << std::setprecision(0) << session_.clock.offsetMs
         << "MS / " << session_.missing.id;
    text(x + 20.0F, missingY + 39.0F, rawB.str(), 0.78F, rgba(0xF1B84B, 0.85F));

    const float rerunY = missingY + 58.0F;
    const Rect rerun{x + 10.0F, rerunY, w - 20.0F, 32.0F};
    rect(rerun.x, rerun.y, rerun.w, rerun.h, rgba(0x173D3E));
    outline(rerun.x, rerun.y, rerun.w, rerun.h, 1.0F, rgba(0x57E0D2, 0.7F));
    triangle(rerun.x + 15.0F, rerun.y + 10.0F, rerun.x + 15.0F, rerun.y + 22.0F,
             rerun.x + 23.0F, rerun.y + 16.0F, cyan);
    text(rerun.x + 30.0F, rerun.y + 10.0F, "RERUN SELECTED", 1.2F, cyan);
    textRight(rerun.x + rerun.w - 10.0F, rerun.y + 11.0F, "R", 1.0F, rgba(0x57E0D2, 0.65F));
    addHit(rerun, Action::RerunSelected);
    const Rect rerunAllBox{x + 10.0F, rerunY + 35.0F, 82.0F, 20.0F};
    rect(rerunAllBox.x, rerunAllBox.y, rerunAllBox.w, rerunAllBox.h, rgba(0x16232E));
    outline(rerunAllBox.x, rerunAllBox.y, rerunAllBox.w, rerunAllBox.h, 1.0F, border);
    textCentered(rerunAllBox.x + rerunAllBox.w * 0.5F, rerunAllBox.y + 6.0F, "RERUN ALL",
                 0.85F, textColor);
    addHit(rerunAllBox, Action::RerunAll);
    const Rect exportObject{x + 102.0F, rerunY + 35.0F, 82.0F, 20.0F};
    rect(exportObject.x, exportObject.y, exportObject.w, exportObject.h, rgba(0x16232E));
    outline(exportObject.x, exportObject.y, exportObject.w, exportObject.h, 1.0F, border);
    textCentered(exportObject.x + exportObject.w * 0.5F, exportObject.y + 6.0F, "EXPORT OBJ",
                 0.85F, rgba(0xB7C8D2));
    addHit(exportObject, Action::ExportObject);

    const ReplayObject& object =
        session_.objects[static_cast<std::size_t>(session_.selectedObject)];
    const float compareY = rerunY + 60.0F;
    rect(x + 10.0F, compareY, w - 20.0F, 78.0F, card);
    outline(x + 10.0F, compareY, w - 20.0F, 78.0F, 1.0F, border);
    text(x + 18.0F, compareY + 9.0F, "COMPARE / " + object.label, 0.9F, muted);
    const RunRecord* selectedRun = nullptr;
    for (const RunRecord& run : object.runs) {
        if (run.sequence == session_.compareSequence) {
            selectedRun = &run;
            break;
        }
    }
    const std::string baselineText =
        !object.baselineKnown ? "MISSING" : (object.baselineVisible ? "VISIBLE" : "CULLED");
    const std::string runText = selectedRun == nullptr
                                    ? "NO RUN"
                                    : (selectedRun->visible ? "VISIBLE" : "CULLED");
    text(x + 18.0F, compareY + 25.0F, "BASE", 0.8F, muted);
    text(x + 18.0F, compareY + 37.0F, baselineText, 1.0F,
         object.baselineKnown ? (object.baselineVisible ? green : red) : amber);
    text(x + 92.0F, compareY + 25.0F,
         selectedRun == nullptr ? "RUN --" : "RUN " + std::to_string(selectedRun->sequence),
         0.8F, muted);
    text(x + 92.0F, compareY + 37.0F, runText, 1.0F,
         selectedRun == nullptr ? muted : (selectedRun->visible ? green : red));
    std::string verdict = "SELECT RUN";
    Color verdictColor = muted;
    if (selectedRun != nullptr) {
        if (!object.baselineKnown) {
            verdict = "RECOVERED";
            verdictColor = green;
        } else if (selectedRun->changed) {
            verdict = "CHANGED";
            verdictColor = amber;
        } else {
            verdict = "MATCH";
            verdictColor = cyan;
        }
    }
    rect(x + 18.0F, compareY + 52.0F, w - 36.0F, 19.0F,
         Color{verdictColor.r, verdictColor.g, verdictColor.b, 0.09F});
    text(x + 24.0F, compareY + 57.0F, verdict, 0.9F, verdictColor);
    textRight(x + w - 24.0F, compareY + 57.0F,
              selectedRun == nullptr ? "C" : shortDigest(selectedRun->inputHash, 8), 0.8F,
              rgba(0x9CB0BE));
    addHit(Rect{x + 10.0F, compareY, w - 20.0F, 78.0F}, Action::CycleCompare);

    const float historyY = compareY + 84.0F;
    text(x + 12.0F, historyY, "RUN HISTORY", 0.85F, muted);
    textRight(x + w - 12.0F, historyY, std::to_string(object.runs.size()) + " SAVED", 0.8F,
              muted);
    const float rowY = historyY + 14.0F;
    const float rowH = 22.0F;
    int shown = 0;
    for (auto it = object.runs.rbegin(); it != object.runs.rend() && shown < 2; ++it, ++shown) {
        const Rect row{x + 10.0F, rowY + static_cast<float>(shown) * rowH, w - 20.0F,
                       rowH - 3.0F};
        const bool active = it->sequence == session_.compareSequence;
        rect(row.x, row.y, row.w, row.h, active ? rgba(0x172B35) : rgba(0x0D151D));
        if (active) {
            rect(row.x, row.y, 2.0F, row.h, cyan);
        }
        std::ostringstream runLabel;
        runLabel << (it->sequence < 10 ? "0" : "") << it->sequence << "  T="
                 << std::fixed << std::setprecision(2) << it->sampleA << "  "
                 << (it->visible ? "VIS" : "CULL");
        text(row.x + 10.0F, row.y + 6.0F, runLabel.str(), 0.85F,
             active ? textColor : rgba(0xB7C8D2));
        textRight(row.x + row.w - 8.0F, row.y + 6.0F, shortDigest(it->inputHash, 6), 0.75F,
                  muted);
        addHit(row, Action::SelectRun, it->sequence);
    }
    if (object.runs.empty()) {
        text(x + 18.0F, rowY + 5.0F, "NO OBJECT-LEVEL RUNS YET", 0.8F, muted);
    }
}

void UiApp::drawTimeline(const Layout& layout) {
    const float x = layout.margin;
    const float y = layout.bottomY;
    const float w = layout.timelineW;
    const float h = layout.bottomH;
    const Color panel = rgba(0x101821);
    const Color border = rgba(0x243543);
    const Color textColor = rgba(0xE8F2F8);
    const Color muted = rgba(0x728797);
    const Color cyan = rgba(0x57E0D2);
    const Color amber = rgba(0xF1B84B);

    rect(x, y, w, h, panel);
    outline(x, y, w, h, 1.0F, border);
    rect(x, y, 3.0F, 36.0F, cyan);
    text(x + 16.0F, y + 13.0F, "SYNCHRONIZED REPLAY", 1.7F, textColor);
    textRight(x + w - 14.0F, y + 15.0F,
              "OFFSET " + millisText(session_.clock.offsetMs) + "MS / CORRECTED A",
              0.9F, amber);

    const Rect playButton{x + 14.0F, y + 30.0F, 25.0F, 24.0F};
    rect(playButton.x, playButton.y, playButton.w, playButton.h,
         session_.playing ? rgba(0x173D3E) : rgba(0x16232E));
    outline(playButton.x, playButton.y, playButton.w, playButton.h, 1.0F,
            session_.playing ? rgba(0x57E0D2, 0.7F) : border);
    if (session_.playing) {
        rect(playButton.x + 8.0F, playButton.y + 7.0F, 3.0F, 10.0F, cyan);
        rect(playButton.x + 14.0F, playButton.y + 7.0F, 3.0F, 10.0F, cyan);
    } else {
        triangle(playButton.x + 9.0F, playButton.y + 7.0F, playButton.x + 9.0F,
                 playButton.y + 17.0F, playButton.x + 17.0F, playButton.y + 12.0F, cyan);
    }
    addHit(playButton, Action::TogglePlay);
    text(x + 48.0F, y + 33.0F, frameTime(session_.playhead), 1.7F, textColor);
    text(x + 174.0F, y + 37.0F, "S", 0.85F, muted);
    textRight(x + 374.0F, y + 37.0F, "PLAYBACK 1.00X", 0.82F, muted);

    const float trackX = x + 56.0F;
    const float trackW = std::max(100.0F, w - 74.0F);
    const float trackAY = y + 66.0F;
    const float trackBY = y + 94.0F;
    const float trackH = 22.0F;
    seekRegion_ = Rect{trackX, trackAY - 4.0F, trackW, 56.0F};
    rect(seekRegion_.x, seekRegion_.y, seekRegion_.w, seekRegion_.h, rgba(0x0A1117));
    outline(seekRegion_.x, seekRegion_.y, seekRegion_.w, seekRegion_.h, 1.0F,
            rgba(0x1F3240));
    for (int tick = 0; tick <= 30; tick += 5) {
        const float tx = trackX + static_cast<float>(tick) / 30.0F * trackW;
        line(tx, trackAY - 4.0F, tx, trackBY + trackH + 2.0F, 1.0F,
             tick % 10 == 0 ? rgba(0x314553) : rgba(0x233541));
        textCentered(tx, trackBY + trackH + 7.0F, std::to_string(tick), 0.75F, muted);
    }
    text(x + 12.0F, trackAY + 6.0F, "A / ALPHA", 0.8F, rgba(0x63D6A0));
    text(x + 12.0F, trackBY + 6.0F, "B / BRAVO", 0.8F, cyan);
    rect(trackX, trackAY, trackW, trackH, rgba(0x14222C));
    rect(trackX, trackBY, trackW, trackH, rgba(0x14222C));

    const float gapAX = trackX + static_cast<float>(session_.missing.startA / 30.0) * trackW;
    const float gapAW = static_cast<float>((session_.missing.endA - session_.missing.startA) /
                                           30.0) * trackW;
    rect(gapAX, trackAY, gapAW, trackH, rgba(0xF1B84B, 0.28F));
    rect(gapAX, trackAY, 1.0F, trackH, amber);
    rect(gapAX + gapAW - 1.0F, trackAY, 1.0F, trackH, amber);
    const double rawBStart = session_.missing.startA + session_.clock.offsetMs / 1000.0;
    const double rawBEnd = session_.missing.endA + session_.clock.offsetMs / 1000.0;
    const float gapBStart = trackX + static_cast<float>(rawBStart / 30.0) * trackW;
    const float gapBEnd = trackX + static_cast<float>(rawBEnd / 30.0) * trackW;
    const float gapBLeft = std::max(trackX, std::min(trackX + trackW, gapBStart));
    const float gapBRight = std::max(trackX, std::min(trackX + trackW, gapBEnd));
    rect(gapBLeft, trackBY, gapBRight - gapBLeft, trackH, rgba(0xF1B84B, 0.19F));
    line(gapBLeft, trackBY, gapBRight, trackBY, 1.0F, rgba(0xF1B84B, 0.7F));
    line(gapBLeft, trackBY + trackH, gapBRight, trackBY + trackH, 1.0F,
         rgba(0xF1B84B, 0.7F));

    const float cursorX = trackX + static_cast<float>(session_.playhead / 30.0) * trackW;
    line(cursorX, trackAY - 5.0F, cursorX, trackBY + trackH + 3.0F, 1.5F, rgba(0xE8F2F8, 0.9F));
    circle(cursorX, trackAY, 3.5F, 14, cyan);
    circle(cursorX, trackBY + trackH, 3.5F, 14, cyan);
    const float gapLabelX = gapAX + gapAW * 0.5F;
    rect(gapLabelX - 38.0F, trackAY - 18.0F, 76.0F, 12.0F, rgba(0x3A2B15));
    textCentered(gapLabelX, trackAY - 16.0F, session_.missing.id, 0.7F, amber);

    int eventCount = 0;
    for (const ReplayObject& object : session_.objects) {
        for (const RunRecord& run : object.runs) {
            const float eventX = trackX + static_cast<float>(run.sampleA / 30.0) * trackW;
            polygon({eventX, trackBY + trackH + 5.0F, eventX + 3.5F,
                     trackBY + trackH + 8.0F, eventX, trackBY + trackH + 11.0F,
                     eventX - 3.5F, trackBY + trackH + 8.0F}, cyan);
            ++eventCount;
        }
    }
    std::ostringstream status;
    status << "A-GAP " << std::fixed << std::setprecision(2)
           << (session_.missing.endA - session_.missing.startA) << "S / B-RAW +"
           << std::setprecision(0) << session_.clock.offsetMs << "MS / " << eventCount
           << " RUN MARKERS";
    text(trackX, trackBY + trackH + 22.0F, status.str(), 0.82F, muted);
    textRight(x + w - 16.0F, y + h - 19.0F,
              "SPACE PLAY / R RERUN / C COMPARE / E EXPORT / ESC SAVE+EXIT", 0.78F,
              rgba(0x728797, 0.9F));
    addHit(seekRegion_, Action::Seek);
}

void UiApp::drawEvidence(const Layout& layout) {
    const float x = layout.evidenceX;
    const float y = layout.bottomY;
    const float w = layout.evidenceW;
    const float h = layout.bottomH;
    const Color panel = rgba(0x101821);
    const Color border = rgba(0x243543);
    const Color textColor = rgba(0xE8F2F8);
    const Color muted = rgba(0x728797);
    const Color cyan = rgba(0x57E0D2);
    const Color green = rgba(0x63D6A0);

    rect(x, y, w, h, panel);
    outline(x, y, w, h, 1.0F, border);
    rect(x + w - 3.0F, y, 3.0F, 36.0F, green);
    text(x + 14.0F, y + 13.0F, "EVIDENCE LEDGER", 1.6F, textColor);
    textRight(x + w - 14.0F, y + 15.0F, std::to_string(session_.evidence.size()) + " ENTRIES",
              0.85F, muted);
    text(x + 14.0F, y + 38.0F, "HEAD", 0.75F, muted);
    text(x + 46.0F, y + 38.0F, shortDigest(evidenceHead(session_.evidence), 16), 0.75F, cyan);

    const int maximumRows = 4;
    const int count = static_cast<int>(session_.evidence.size());
    const int rows = std::min(maximumRows, count);
    const float rowsTop = y + 52.0F;
    const float rowH = std::max(27.0F, (h - 84.0F) / static_cast<float>(maximumRows));
    const float lineX = x + 20.0F;
    if (rows > 0) {
        line(lineX, rowsTop + 5.0F, lineX, rowsTop + rowH * static_cast<float>(rows) - 5.0F,
             1.0F, rgba(0x57E0D2, 0.28F));
    }
    for (int row = 0; row < rows; ++row) {
        const EvidenceEntry& entry =
            session_.evidence[static_cast<std::size_t>(count - 1 - row)];
        const float rowY = rowsTop + static_cast<float>(row) * rowH;
        const bool latest = row == 0;
        circle(lineX, rowY + 8.0F, latest ? 3.5F : 2.5F, 12,
               latest ? cyan : rgba(0x5A7181));
        std::string time = entry.at.size() >= 19U ? entry.at.substr(11, 8) : entry.at;
        text(x + 30.0F, rowY + 3.0F, time, 0.78F, muted);
        text(x + 78.0F, rowY + 3.0F, clipText(entry.kind, 20), 0.78F,
             latest ? textColor : rgba(0xB8C9D3));
        if (!entry.objectId.empty()) {
            textRight(x + w - 12.0F, rowY + 3.0F, entry.objectId, 0.7F, cyan);
        }
        text(x + 30.0F, rowY + 14.0F, clipText(entry.detail, 49), 0.68F,
             rgba(0x7F95A4, 0.95F));
    }
    rect(x + 12.0F, y + h - 29.0F, w - 24.0F, 1.0F, border);
    circle(x + 18.0F, y + h - 14.0F, 3.0F, 12, dirty_ ? rgba(0xF1B84B) : green);
    text(x + 26.0F, y + h - 17.0F,
         dirty_ ? "DIRTY / AUTOSAVE 2S / ATOMIC WRITE"
                : "PERSISTED / AUTOSAVE 2S / ATOMIC WRITE",
         0.72F, dirty_ ? rgba(0xF1B84B) : rgba(0x63D6A0));
}

void UiApp::drawToast(const Layout& layout) {
    if (toastTimer_ <= 0.0 || toast_.empty() || pendingExport_ || snapshotOnly_) {
        return;
    }
    std::string message = clipText(toast_, 84);
    const float scale = 1.05F;
    float width = textWidth(message, scale) + 48.0F;
    width = std::min(width, layout.width - 80.0F);
    const float height = 32.0F;
    const float x = (layout.width - width) * 0.5F;
    const float y = layout.height - 50.0F;
    const Color accent = toastError_ ? rgba(0xF06A6A) : rgba(0x57E0D2);
    rect(x, y, width, height, rgba(0x101C25, 0.96F));
    outline(x, y, width, height, 1.0F, Color{accent.r, accent.g, accent.b, 0.55F});
    rect(x, y, 3.0F, height, accent);
    text(x + 14.0F, y + 12.0F, message, scale, toastError_ ? rgba(0xFFB2B2) : rgba(0xE8F2F8));
    const float progress = static_cast<float>(toastTimer_ / 5.0);
    rect(x + 3.0F, y + height - 2.0F, (width - 3.0F) * progress, 2.0F,
         Color{accent.r, accent.g, accent.b, 0.55F});
}

}  // namespace vfcr







