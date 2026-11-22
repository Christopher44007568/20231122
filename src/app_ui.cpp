#include "app.hpp"

#include "params.hpp"
#include "ui.hpp"

#include <iomanip>
#include <sstream>

namespace {

std::string objectStatus(const ObjectRun& run) {
  if (!run.extreme.valid) {
    return "pending";
  }
  std::ostringstream out;
  out << run.extreme.compare.missingPixels << "->"
      << (run.hasCorrected ? run.corrected.compare.missingPixels
                           : run.extreme.compare.missingPixels)
      << " missing";
  return out.str();
}

const char* modeName(int mode) {
  switch (mode) {
    case 0: return "BASELINE";
    case 1: return "RERUN / CORRECTED";
    case 2: return "SIDE BY SIDE";
    case 3: return "FRAGMENT DIFF";
    default: return "UNKNOWN";
  }
}

}  // namespace

UiFrame Application::buildUi() const {
  UiBuilder ui(width_, height_);
  const int panelX = 12;
  const int panelY = 12;
  const int panelWidth = 424;
  const int panelHeight = height_ - 24;
  ui.panel(panelX, panelY, panelWidth, panelHeight, ui_palette::Panel);
  ui.outline(panelX, panelY, panelWidth, panelHeight, {44, 65, 78, 255}, 1);
  ui.panel(panelX, panelY, 4, 72, ui_palette::Accent);

  const int left = panelX + 18;
  ui.text(left, panelY + 12, "DEPTH RECHECK", ui_palette::Text, 2);
  ui.text(left, panelY + 40, "screen pick / local correction",
          ui_palette::Muted);
  ui.panel(left, panelY + 58, panelWidth - 36, 1, {52, 74, 88, 255});

  const SessionState& state = session_.state();
  ui.text(left, panelY + 68, "session  " + state.sessionId,
          ui_palette::Muted);
  ui.text(left, panelY + 84,
          std::string("checkpoint  ") + (state.resumed ? "RESUMED" : "FRESH") +
            "   objects " + std::to_string(state.runs.size()),
          state.resumed ? ui_palette::Success : ui_palette::Accent);

  const DepthParams& preset = presetAt(state.presetIndex);
  ui.text(left, panelY + 108,
          "PRESET " + std::to_string(state.presetIndex + 1) + "/" +
            std::to_string(kPresetCount) + "  " + preset.name,
          ui_palette::Warning);
  ui.text(left, panelY + 124, presetSummary(preset), ui_palette::Muted);

  ui.text(left, panelY + 148, "OBJECTS  click / Up-Down / Tab",
          ui_palette::Text);
  int lineY = panelY + 166;
  for (std::size_t index = 0; index < state.runs.size(); ++index) {
    const ObjectRun& run = state.runs[index];
    const bool selected = static_cast<int>(index) == state.selectedIndex;
    std::ostringstream line;
    line << (selected ? "> " : "  ") << std::setw(2) << std::setfill('0')
         << run.id << " " << std::setfill(' ') << std::left << std::setw(16)
         << run.name << " "
         << objectStatus(run);
    ui.text(left, lineY, line.str(),
            selected ? ui_palette::Accent : ui_palette::Muted);
    lineY += 18;
  }

  const ObjectRun* run = selectedRun();
  lineY += 10;
  ui.panel(left, lineY - 6, panelWidth - 36, 1, {52, 74, 88, 255});
  ui.text(left, lineY + 2, "SELECTED EVIDENCE", ui_palette::Text);
  lineY += 22;
  if (run != nullptr && run->extreme.valid) {
    ui.text(left, lineY,
            "focus pixels " + std::to_string(run->extreme.compare.focusPixels) +
              "   changed " + std::to_string(run->extreme.compare.changedPixels),
            ui_palette::Muted);
    lineY += 18;
    ui.text(left, lineY,
            "extreme    " + run->extreme.compare.classification + "  missing " +
              std::to_string(run->extreme.compare.missingPixels) + "  added " +
              std::to_string(run->extreme.compare.addedPixels),
            run->extreme.compare.missingPixels > 0 ? ui_palette::Danger
                                                   : ui_palette::Success);
    lineY += 18;
    if (run->hasCorrected) {
      ui.text(left, lineY,
              "corrected  " + run->corrected.compare.classification + "  missing " +
                std::to_string(run->corrected.compare.missingPixels) + "  added " +
                std::to_string(run->corrected.compare.addedPixels),
              run->corrected.compare.missingPixels > 0 ? ui_palette::Warning
                                                       : ui_palette::Success);
      lineY += 18;
      ui.text(left, lineY,
              "bias " + formatFloat(run->extreme.bias.constant, 1) + " -> " +
                formatFloat(run->corrected.bias.constant, 1) + "   depth delta " +
                formatFloat(run->corrected.compare.meanAbsDepthDelta, 4),
              ui_palette::Muted);
      lineY += 18;
    }
  } else {
    ui.text(left, lineY, "No replay captured yet. Press R.", ui_palette::Muted);
    lineY += 18;
  }

  const CompareResult* boundaryResult = nullptr;
  if (run != nullptr) {
    if (run->hasCorrected && !run->corrected.compare.boundary.empty()) {
      boundaryResult = &run->corrected.compare;
    } else if (!run->extreme.compare.boundary.empty()) {
      boundaryResult = &run->extreme.compare;
    }
  }
  ui.text(left, lineY + 4, "BOUNDARY BEFORE / AFTER", ui_palette::Text);
  lineY += 24;
  if (boundaryResult != nullptr) {
    for (const BoundarySample& sample : boundaryResult->boundary) {
      std::ostringstream line;
      line << std::left << std::setw(14) << sample.label << " "
           << sample.point.x << "," << sample.point.y << "  "
           << sample.before.objectId << "@" << formatFloat(sample.before.depth, 2)
           << " -> " << sample.after.objectId << "@"
           << formatFloat(sample.after.depth, 2)
           << (sample.objectChanged ? " *" : "");
      ui.text(left, lineY, line.str(),
              sample.objectChanged ? ui_palette::Warning : ui_palette::Muted);
      lineY += 17;
    }
  } else {
    ui.text(left, lineY, "No boundary samples", ui_palette::Muted);
    lineY += 17;
  }

  lineY = height_ - 204;
  const bool evidenceReady = run != nullptr && run->evidenceComplete;
  ui.text(left, lineY - 18,
          evidenceReady ? "evidence packet complete" : "evidence pending",
          evidenceReady ? ui_palette::Success : ui_palette::Warning);
  ui.text(left, lineY, "R / F5  replay selected + auto-correct", ui_palette::Text);
  ui.text(left, lineY + 17, "A       replay + export every object", ui_palette::Text);
  ui.text(left, lineY + 34, "P       next extreme preset", ui_palette::Text);
  ui.text(left, lineY + 51, "1-4     baseline/rerun/compare/diff", ui_palette::Text);
  ui.text(left, lineY + 68, "E       export selected evidence", ui_palette::Text);
  ui.text(left, lineY + 85, "F2      save checkpoint", ui_palette::Text);
  ui.text(left, lineY + 102, "Esc     save and exit", ui_palette::Text);

  const int cardX = width_ - 356;
  ui.panel(cardX, 16, 340, 76, ui_palette::Panel);
  ui.outline(cardX, 16, 340, 76, {44, 65, 78, 255}, 1);
  ui.text(cardX + 14, 28, runningAll_ ? "RUNNING ALL" : modeName(state.viewMode),
          ui_palette::Accent, 2);
  std::string shortStatus = overlayMessage_;
  if (shortStatus.size() > 57) {
    shortStatus = shortStatus.substr(0, 54) + "...";
  }
  ui.text(cardX + 14, 58, shortStatus, ui_palette::Text);
  ui.text(cardX + 14, 74,
          "cursor " + std::to_string(state.cursor.x) + "," +
            std::to_string(state.cursor.y) + "   " + preset.name,
          ui_palette::Muted);

  if (run != nullptr && run->extreme.compare.expectedBounds.has_value()) {
    const RectI bounds = *run->extreme.compare.expectedBounds;
    ui.outline(bounds.x, bounds.y, bounds.width, bounds.height,
               evidenceReady ? ui_palette::Success : ui_palette::Warning, 1);
  }
  if (boundaryResult != nullptr) {
    for (const BoundarySample& sample : boundaryResult->boundary) {
      ui.marker(sample.point.x, sample.point.y, 3,
                sample.objectChanged ? ui_palette::Danger : ui_palette::Accent);
    }
  }
  ui.marker(state.cursor.x, state.cursor.y, 7, ui_palette::Accent);
  return ui.finish();
}

