#include "app/app_state.h"

#include "core/time_util.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace clipfix {
namespace {

void sectionLabel(const char* text) {
    ImGui::Spacing();
    ImGui::SeparatorText(text);
}

void objectStatusText(const ObjectState& object) {
    if (object.runs.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.64f, 0.28f, 1.0f), "NOT RUN");
    } else if (object.dirty) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "OFFSETS EDITED");
    } else {
        ImGui::TextColored(ImVec4(0.42f, 0.92f, 0.56f, 1.0f), "READY");
    }
}

std::string objectHeading(const RawObject& object) {
    return object.name + "  [" + object.id + "]";
}

}  // namespace

void drawUi(AppState& app, double nowSeconds) {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(448.0f, std::max(360.0f, io.DisplaySize.y - 24.0f)),
        ImGuiCond_Always);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize;
    ImGui::Begin("Curved Trim Plane", nullptr, flags);

    ImGui::TextWrapped(
        "Three-anchor curved trim plane. Every render object keeps an "
        "independent run history, comparison and export.");
    ImGui::TextWrapped(
        "C++17 / Vulkan 1.1 / GLFW 3.4   |   Session auto-restores");
    ImGui::Spacing();

    const auto& state = app.workspace.state();
    ImGui::TextWrapped("Workspace  %s", app.workspace.root().string().c_str());
    ImGui::Text("Session    rev %d  resumes %d", state.revision, state.resumeCount);
    ImGui::Text("Raw SHA    %.16s...", app.workspace.aggregateSha256().c_str());
    if (!state.lastResumeAt.empty()) {
        ImGui::TextColored(
            ImVec4(0.48f, 0.86f, 1.0f, 1.0f),
            "Resumed at %s",
            state.lastResumeAt.c_str());
    }

    sectionLabel("Three anchors / original offsets");
    const auto positions = app.workspace.anchorPositions();
    const auto rawOffsets = app.workspace.rawOffsets();
    for (std::size_t i = 0; i < 3; ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text(
            "%s   pos (%.3f, %.3f, %.3f)",
            app.workspace.material().scene.anchors[i].name.c_str(),
            positions[i].x,
            positions[i].y,
            positions[i].z);
        ImGui::TextDisabled(
            "raw offset %s",
            formatMm(rawOffsets[i]).c_str());
        float value = static_cast<float>(state.currentOffsetsMm[i]);
        ImGui::SetNextItemWidth(190.0f);
        if (ImGui::SliderFloat("applied offset (mm)", &value, -1.0f, 1.0f, "%.5f")) {
            app.workspace.setCurrentOffset(i, static_cast<double>(value));
            app.renderDirty = true;
        }
        const bool committed = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        if (ImGui::SmallButton("Raw")) {
            app.workspace.setCurrentOffset(i, rawOffsets[i]);
            app.renderDirty = true;
            app.actions |= UiActionSave;
        }
        if (committed) {
            app.actions |= UiActionSave;
        }
        ImGui::PopID();
    }

    sectionLabel("Render objects / independent evidence");
    if (ImGui::Button("Run selected", ImVec2(132.0f, 0.0f))) {
        app.actions |= UiActionRunSelected;
    }
    ImGui::SameLine();
    if (ImGui::Button("Run all", ImVec2(98.0f, 0.0f))) {
        app.actions |= UiActionRunAll;
    }
    ImGui::SameLine();
    if (ImGui::Button("Export all", ImVec2(100.0f, 0.0f))) {
        app.actions |= UiActionExportAll;
    }

    ImGui::BeginChild(
        "objects",
        ImVec2(0.0f, 300.0f),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    for (std::size_t i = 0; i < app.workspace.material().scene.objects.size(); ++i) {
        const auto& rawObject = app.workspace.material().scene.objects[i];
        const auto* object = app.workspace.findObjectState(rawObject.id);
        ImGui::PushID(static_cast<int>(i));
        const bool selected = state.selectedObject == static_cast<int>(i);
        if (ImGui::Selectable(objectHeading(rawObject).c_str(), selected)) {
            app.workspace.state().selectedObject = static_cast<int>(i);
            app.renderDirty = true;
        }
        objectStatusText(*object);
        if (object->runs.empty()) {
            ImGui::TextDisabled("No captured run yet.");
        } else {
            const auto& run = object->runs.back();
            ImGui::Text(
                "last %s  affected %zu/%zu",
                run.finishedUtc.c_str(),
                run.stats.affectedCount,
                run.stats.vertexCount);
            ImGui::Text(
                "raw %s | applied %s",
                formatFixed(run.rawOffsetsMm[0]).c_str(),
                formatFixed(run.appliedOffsetsMm[0]).c_str());
            ImGui::Text(
                "max delta %s   runs %zu",
                formatMm(run.stats.maxDeltaMm).c_str(),
                object->runs.size());
        }
        if (ImGui::SmallButton("Re-run")) {
            app.workspace.state().selectedObject = static_cast<int>(i);
            app.actions |= UiActionRunSelected;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Compare")) {
            app.workspace.state().selectedObject = static_cast<int>(i);
            app.actions |= UiActionCompareSelected;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Export")) {
            app.workspace.state().selectedObject = static_cast<int>(i);
            app.actions |= UiActionExportSelected;
        }
        if (object->hasCompare) {
            ImGui::TextDisabled(
                "compare %s -> %s, max %s",
                object->lastCompare.aKind.c_str(),
                object->lastCompare.bKind.c_str(),
                formatMm(object->lastCompare.maxDeltaMm).c_str());
        }
        if (object->hasExport) {
            ImGui::TextDisabled("exported %s", object->lastExport.exportedUtc.c_str());
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();

    sectionLabel("Display");
    int mode = static_cast<int>(app.viewMode);
    if (ImGui::RadioButton("Corrected", &mode, 0)) {
        app.viewMode = ViewMode::Corrected;
        app.renderDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Original", &mode, 1)) {
        app.viewMode = ViewMode::Original;
        app.renderDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Diff", &mode, 2)) {
        app.viewMode = ViewMode::Diff;
        app.renderDirty = true;
    }
    if (ImGui::Checkbox("Clip surface", &app.showClipSurface)) {
        app.renderDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Wireframe", &app.showWireframe)) {
        app.renderDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset view")) {
        app.orbitYawDeg = -28.0f;
        app.orbitPitchDeg = 18.0f;
        app.orbitDistance = 6.4f;
    }
    ImGui::TextDisabled("Drag viewport to orbit, wheel to zoom.");

    sectionLabel("Evidence");
    if (!state.lastSuspendAt.empty()) {
        ImGui::TextDisabled("last suspend: %s", state.lastSuspendAt.c_str());
    }
    ImGui::TextWrapped(
        "append-only chain: %s/evidence/evidence.jsonl",
        app.workspace.root().string().c_str());
    if (ImGui::Button("Write acceptance report", ImVec2(220.0f, 0.0f))) {
        app.actions |= UiActionAcceptance;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(74.0f, 0.0f))) {
        app.actions |= UiActionSave;
    }

    if (!app.activity.empty()) {
        ImGui::BeginChild(
            "activity",
            ImVec2(0.0f, 92.0f),
            ImGuiChildFlags_Borders,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);
        for (const auto& line : app.activity) {
            ImGui::TextWrapped("%s", line.c_str());
        }
        ImGui::EndChild();
    }

    if (nowSeconds < app.noticeUntil && !app.notice.empty()) {
        if (app.noticeIsError) {
            ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.32f, 1.0f), "%s", app.notice.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.42f, 0.92f, 0.56f, 1.0f), "%s", app.notice.c_str());
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Save & Exit", ImVec2(150.0f, 34.0f))) {
        app.actions |= UiActionSaveExit;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Esc saves and exits.");

    ImGui::End();
}

}  // namespace clipfix
