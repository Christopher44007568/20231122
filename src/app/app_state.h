#pragma once

#include "core/workspace.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace clipfix {

enum class ViewMode {
    Corrected = 0,
    Original = 1,
    Diff = 2,
};

enum UiAction : std::uint32_t {
    UiActionNone = 0,
    UiActionRunSelected = 1U << 0U,
    UiActionRunAll = 1U << 1U,
    UiActionCompareSelected = 1U << 2U,
    UiActionExportSelected = 1U << 3U,
    UiActionExportAll = 1U << 4U,
    UiActionAcceptance = 1U << 5U,
    UiActionSave = 1U << 6U,
    UiActionSaveExit = 1U << 7U,
};

struct AppState {
    explicit AppState(Workspace opened)
        : workspace(std::move(opened)) {}

    Workspace workspace;
    ViewMode viewMode = ViewMode::Corrected;
    bool showClipSurface = true;
    bool showWireframe = false;
    float orbitYawDeg = -28.0f;
    float orbitPitchDeg = 18.0f;
    float orbitDistance = 6.4f;
    std::uint32_t actions = UiActionNone;
    bool renderDirty = true;
    bool requestClose = false;
    std::string notice;
    bool noticeIsError = false;
    double noticeUntil = 0.0;
    std::vector<std::string> activity;

    std::string selectedObjectId() const {
        if (workspace.state().objects.empty()) {
            return {};
        }
        return workspace.state().objects[workspace.state().selectedObject].id;
    }

    void log(std::string message, bool error = false) {
        if (error) {
            message = "ERROR: " + message;
        }
        activity.push_back(std::move(message));
        if (activity.size() > 14) {
            activity.erase(activity.begin());
        }
    }

    void setNotice(std::string message, bool error, double nowSeconds) {
        notice = std::move(message);
        noticeIsError = error;
        noticeUntil = nowSeconds + 5.0;
        log(notice, error);
    }
};

void drawUi(AppState& app, double nowSeconds);

}  // namespace clipfix
