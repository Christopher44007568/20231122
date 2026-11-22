#pragma once

#include "imgui.h"

namespace aa {

class UiApp {
public:
    void Initialize(ImGuiIO& io);
    void Draw();

private:
    struct FontSet {
        ImFont* body = nullptr;
        ImFont* medium = nullptr;
        ImFont* small = nullptr;
        ImFont* label = nullptr;
        ImFont* title = nullptr;
        ImFont* hero = nullptr;
        ImFont* mono = nullptr;
        ImFont* monoSmall = nullptr;
    };

    void LoadFonts(ImGuiIO& io);
    void DrawSidebar(const ImVec2& size);
    void DrawTopBar(const ImVec2& size);
    void DrawHeader(const ImVec2& size);
    void DrawStatStrip(const ImVec2& size);
    void DrawReplayCard(const ImVec2& min, const ImVec2& max);
    void DrawUnknownCard(const ImVec2& min, const ImVec2& max);
    void DrawDecisionCard(const ImVec2& min, const ImVec2& max);
    void DrawEvidenceCard(const ImVec2& min, const ImVec2& max);
    void DrawRunbookCard(const ImVec2& min, const ImVec2& max);
    void DrawEvidenceModal(const ImVec2& size);
    void DrawFailureModal(const ImVec2& size);
    void DrawToast(const ImVec2& size);

    FontSet fonts_;
    bool humanConfirmed_ = false;
    bool supplementRequested_ = false;
    bool pendingReview_ = false;
    int evidenceModal_ = -1;
    bool failureModal_ = false;
    char toast_[192] = {};
    double toastUntil_ = 0.0;
};

} // namespace aa
