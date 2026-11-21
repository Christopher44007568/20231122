#pragma once

#include "app/render_types.hpp"
#include "core/replay.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vfcr {

struct UiFrame {
    std::vector<UiVertex> vertices;
    bool capture = false;
    std::string capturePath;
    std::string exportDirectory;
};

class UiApp {
public:
    UiApp(Session session, std::string statePath, std::string startupMessage);

    void tick(double deltaSeconds);
    UiFrame buildFrame(int width, int height);
    void pointerDown(double x, double y);
    void pointerMove(double x, double y);
    void pointerUp();
    void keyDown(int key, int mods);
    void scroll(double offset);
    void finishExport(const CaptureResult& capture);
    void saveNow();
    void requestSnapshot(std::string path);

    bool exitRequested() const { return exitRequested_; }
    bool snapshotDone() const { return snapshotDone_; }
    const Session& session() const { return session_; }

private:
    struct Color {
        float r = 1.0F;
        float g = 1.0F;
        float b = 1.0F;
        float a = 1.0F;
    };
    struct Rect {
        float x = 0.0F;
        float y = 0.0F;
        float w = 0.0F;
        float h = 0.0F;
        bool contains(double px, double py) const;
    };
    enum class Action {
        None,
        SelectObject,
        TogglePlay,
        Seek,
        RerunSelected,
        RerunAll,
        OffsetDown,
        OffsetUp,
        GapEarlier,
        GapLater,
        SelectRun,
        Save,
        Export,
        ExportObject,
        CycleCompare,
    };
    struct Hit {
        Rect rect;
        Action action = Action::None;
        int index = 0;
    };
    struct Layout {
        float width = 0.0F;
        float height = 0.0F;
        float margin = 16.0F;
        float gap = 12.0F;
        float top = 64.0F;
        float mainY = 78.0F;
        float mainH = 430.0F;
        float leftX = 16.0F;
        float leftW = 278.0F;
        float centerX = 0.0F;
        float centerW = 0.0F;
        float rightX = 0.0F;
        float rightW = 252.0F;
        float bottomY = 520.0F;
        float bottomH = 224.0F;
        float timelineW = 0.0F;
        float evidenceW = 310.0F;
        float evidenceX = 0.0F;
    };

    Layout layoutFor(int width, int height) const;
    void drawChrome(const Layout& layout);
    void drawTopBar(const Layout& layout);
    void drawObjectList(const Layout& layout);
    void drawFrustum(const Layout& layout);
    void drawCorrectionPanel(const Layout& layout);
    void drawTimeline(const Layout& layout);
    void drawEvidence(const Layout& layout);
    void drawToast(const Layout& layout);

    void addHit(const Rect& rect, Action action, int index = 0);
    void trigger(Action action, int index, double x, double y);
    void selectObject(int index);
    void rerunSelected();
    void rerunAll();
    void tweakOffset(double deltaMs);
    void shiftGap(double deltaSeconds);
    void cycleCompare();
    void queueExport(bool focusSelected = false);
    void markDirty();
    void setToast(const std::string& message, bool error = false);

    void rect(float x, float y, float w, float h, Color color);
    void outline(float x, float y, float w, float h, float thickness, Color color);
    void line(float x1, float y1, float x2, float y2, float thickness, Color color);
    void dashedLine(float x1, float y1, float x2, float y2, float thickness, float dash,
                    float gap, Color color);
    void polygon(const std::vector<float>& xy, Color color);
    void triangle(float x1, float y1, float x2, float y2, float x3, float y3, Color color);
    void circle(float cx, float cy, float radius, int segments, Color color);
    void ring(float cx, float cy, float radius, float thickness, int segments, Color color);
    void text(float x, float y, const std::string& value, float scale, Color color);
    void textRight(float right, float y, const std::string& value, float scale, Color color);
    void textCentered(float cx, float y, const std::string& value, float scale, Color color);
    float textWidth(const std::string& value, float scale) const;

    static Color rgba(std::uint32_t hex, float alpha = 1.0F);
    static std::string frameTime(double seconds);
    static std::string millisText(double value);
    static std::string uppercase(std::string value);

    Session session_;
    std::string statePath_;
    std::string startupMessage_;
    std::vector<UiVertex> vertices_;
    std::vector<Hit> hits_;
    double mouseX_ = 0.0;
    double mouseY_ = 0.0;
    bool draggingTimeline_ = false;
    Rect seekRegion_;
    double saveAccumulator_ = 0.0;
    bool dirty_ = false;
    bool exitRequested_ = false;
    bool pendingExport_ = false;
    std::string pendingExportDirectory_;
    std::string pendingFocusObjectId_;
    std::string snapshotPath_;
    bool snapshotOnly_ = false;
    bool snapshotDone_ = false;
    std::string toast_;
    bool toastError_ = false;
    double toastTimer_ = 0.0;
    double pulse_ = 0.0;
};

}  // namespace vfcr
