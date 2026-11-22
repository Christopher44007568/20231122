#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app.hpp"

#include "image_io.hpp"
#include "params.hpp"
#include "report.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace {

std::string candidateKey(const BiasCandidate& candidate) {
  std::ostringstream out;
  out << std::setprecision(9) << candidate.constant << "/" << candidate.slope;
  return out.str();
}

}  // namespace

const ObjectRun* Application::selectedRun() const {
  const auto& runs = session_.state().runs;
  const int index = session_.state().selectedIndex;
  if (index < 0 || index >= static_cast<int>(runs.size())) {
    return nullptr;
  }
  return &runs[static_cast<std::size_t>(index)];
}

ObjectRun* Application::selectedRun() {
  auto& runs = session_.state().runs;
  const int index = session_.state().selectedIndex;
  if (index < 0 || index >= static_cast<int>(runs.size())) {
    return nullptr;
  }
  return &runs[static_cast<std::size_t>(index)];
}

std::string Application::formatFloat(float value, int precision) const {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}

std::string Application::statusLine(const ObjectRun& run) const {
  if (!run.extreme.valid) {
    return "pending";
  }
  std::ostringstream out;
  out << run.extreme.compare.classification << " / "
      << run.extreme.compare.missingPixels << " missing";
  if (run.hasCorrected) {
    out << " -> " << run.corrected.compare.classification << " / "
        << run.corrected.compare.missingPixels;
  }
  return out.str();
}

void Application::markDirty() { dirty_ = true; }

void Application::cyclePreset() {
  SessionState& state = session_.state();
  state.presetIndex = (state.presetIndex + 1) % kPresetCount;
  overlayMessage_ = "Preset: " + presetAt(state.presetIndex).name;
  std::string error;
  if (!rerunSelected(error)) {
    overlayMessage_ = "Preset replay failed: " + error;
  }
  dirty_ = true;
}

void Application::cycleView() {
  SessionState& state = session_.state();
  state.viewMode = (state.viewMode + 1) % 4;
  overlayMessage_ = "View mode " + std::to_string(state.viewMode);
  std::string error;
  saveCheckpoint(error);
  dirty_ = true;
}

bool Application::rerunSelected(std::string& error) {
  return rerunObject(session_.state().selectedIndex, true, error);
}

bool Application::rerunObject(int index, bool exportEvidence,
                              std::string& error) {
  SessionState& state = session_.state();
  if (index < 0 || index >= static_cast<int>(state.runs.size())) {
    error = "Object index out of range";
    return false;
  }
  ObjectRun& run = state.runs[static_cast<std::size_t>(index)];
  const DepthParams& preset = presetAt(state.presetIndex);
  const BiasCandidate extremeBias{preset.depthBiasConstant,
                                  preset.depthBiasSlope};
  overlayMessage_ = "Replaying " + run.name + " with " + preset.name;

  FrameData extremeOutput;
  double extremeMs = 0.0;
  if (!renderer_.renderScenario(preset, run.id, extremeBias, extremeOutput,
                                extremeMs, error)) {
    return false;
  }
  extremeFrame_ = extremeOutput;
  run.extreme.valid = true;
  run.extreme.bias = extremeBias;
  run.extreme.elapsedMs = extremeMs;
  run.extreme.compare = compareFrames(renderer_.baselineFrame(), extremeFrame_, run.id);
  run.extreme.score = candidateScore(run.extreme.compare);

  std::vector<CandidateEvaluation> evaluations;
  evaluations.push_back({extremeBias, run.extreme.compare,
                         run.extreme.score, false});
  std::set<std::string> seen;
  seen.insert(candidateKey(extremeBias));
  double searchMs = extremeMs;
  for (const BiasCandidate& candidate : correctionCandidates(preset)) {
    if (!seen.insert(candidateKey(candidate)).second) {
      continue;
    }
    FrameData candidateFrame;
    double candidateMs = 0.0;
    if (!renderer_.renderScenario(preset, run.id, candidate, candidateFrame,
                                  candidateMs, error)) {
      return false;
    }
    CompareResult candidateCompare =
      compareFrames(renderer_.baselineFrame(), candidateFrame, run.id);
    CandidateEvaluation evaluation;
    evaluation.bias = candidate;
    evaluation.score = candidateScore(candidateCompare);
    evaluation.compare = std::move(candidateCompare);
    evaluations.push_back(std::move(evaluation));
    searchMs += candidateMs;
  }
  const auto best = std::min_element(
    evaluations.begin(), evaluations.end(),
    [](const CandidateEvaluation& left, const CandidateEvaluation& right) {
      if (std::abs(left.score - right.score) > 1e-6) {
        return left.score < right.score;
      }
      const float leftMagnitude =
        std::abs(left.bias.constant) + std::abs(left.bias.slope) * 16.0f;
      const float rightMagnitude =
        std::abs(right.bias.constant) + std::abs(right.bias.slope) * 16.0f;
      return leftMagnitude < rightMagnitude;
    });
  best->selected = true;

  FrameData correctedOutput;
  double finalMs = 0.0;
  if (!renderer_.renderScenario(preset, run.id, best->bias, correctedOutput,
                                finalMs, error)) {
    return false;
  }
  correctedFrame_ = correctedOutput;
  run.hasCorrected = true;
  run.corrected.valid = true;
  run.corrected.bias = best->bias;
  run.corrected.elapsedMs = searchMs + finalMs;
  run.corrected.compare =
    compareFrames(renderer_.baselineFrame(), correctedFrame_, run.id);
  run.corrected.score = candidateScore(run.corrected.compare);
  best->compare = run.corrected.compare;
  lastCandidates_ = evaluations;
  run.evidenceDir = session_.objectEvidenceDir(index).generic_string();

  if (exportEvidence && !exportObjectEvidence(index, error)) {
    return false;
  }
  std::ostringstream note;
  note << "Replayed " << run.name << "; extreme missing="
       << run.extreme.compare.missingPixels << ", corrected missing="
       << run.corrected.compare.missingPixels;
  state.note = note.str();
  overlayMessage_ = note.str();
  dirty_ = true;
  return saveCheckpoint(error);
}

bool Application::ensureBaselineEvidence(std::string& error) {
  const std::filesystem::path path = session_.baselineEvidencePath();
  if (std::filesystem::exists(path)) {
    return true;
  }
  return writePng(path, renderer_.baselineFrame(), error);
}

bool Application::exportObjectEvidence(int index, std::string& error) {
  SessionState& state = session_.state();
  if (index < 0 || index >= static_cast<int>(state.runs.size())) {
    error = "Cannot export invalid object index";
    return false;
  }
  if (extremeFrame_.empty() || correctedFrame_.empty()) {
    error = "No captured frames for selected object";
    return false;
  }
  ObjectRun& run = state.runs[static_cast<std::size_t>(index)];
  const std::filesystem::path directory = session_.objectEvidenceDir(index);
  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  if (ec) {
    error = "Cannot create evidence directory: " + ec.message();
    return false;
  }
  if (!ensureBaselineEvidence(error) ||
      !writePng(directory / "extreme.png", extremeFrame_, error) ||
      !writePng(directory / "corrected.png", correctedFrame_, error) ||
      !writePng(directory / "diff.png",
                buildDiffFrame(renderer_.baselineFrame(), extremeFrame_, run.id),
                error) ||
      !writePng(directory / "corrected_diff.png",
                buildDiffFrame(renderer_.baselineFrame(), correctedFrame_, run.id),
                error) ||
      !writePng(directory / "compare.png",
                buildSideBySideFrame(renderer_.baselineFrame(), correctedFrame_),
                error) ||
      !writeObjectEvidence(directory, run, presetAt(state.presetIndex),
                           lastCandidates_, session_.baselineEvidencePath(),
                           error)) {
    return false;
  }
  run.evidenceDir = directory.generic_string();
  run.evidenceComplete = true;

  std::ofstream history(session_.root() / "evidence" / "runs.log",
                        std::ios::app);
  if (history) {
    history << utcNow() << "|" << run.id << "|" << run.name << "|"
            << presetAt(state.presetIndex).name << "|"
            << run.extreme.compare.missingPixels << "|"
            << run.extreme.compare.addedPixels << "|"
            << run.corrected.compare.missingPixels << "|"
            << run.corrected.compare.addedPixels << "|"
            << run.corrected.bias.constant << "|" << run.corrected.bias.slope
            << "\n";
  }
  return true;
}

void Application::onKey(int key, int action) {
  if (action != GLFW_PRESS && action != GLFW_REPEAT) {
    return;
  }
  if (key == GLFW_KEY_ESCAPE) {
    glfwSetWindowShouldClose(window_, GLFW_TRUE);
    return;
  }
  if (runningAll_) {
    return;
  }
  if (key == GLFW_KEY_V || key == GLFW_KEY_SPACE) {
    cycleView();
    return;
  }
  if (key == GLFW_KEY_P) {
    cyclePreset();
    return;
  }
  if (key == GLFW_KEY_F2 && action == GLFW_PRESS) {
    std::string error;
    overlayMessage_ = saveCheckpoint(error) ? "Checkpoint saved"
                                            : "Save failed: " + error;
    dirty_ = true;
    return;
  }
  if (key == GLFW_KEY_E) {
    std::string error;
    if (exportObjectEvidence(session_.state().selectedIndex, error) &&
        saveCheckpoint(error)) {
      overlayMessage_ = "Exported selected object evidence";
    } else {
      overlayMessage_ = "Export failed: " + error;
    }
    dirty_ = true;
    return;
  }
  if (key == GLFW_KEY_UP || key == GLFW_KEY_W ||
      key == GLFW_KEY_DOWN || key == GLFW_KEY_S || key == GLFW_KEY_TAB) {
    const int count = static_cast<int>(session_.state().runs.size());
    if (count == 0) {
      return;
    }
    const bool forward = key == GLFW_KEY_DOWN || key == GLFW_KEY_S ||
                         key == GLFW_KEY_TAB;
    int& selected = session_.state().selectedIndex;
    selected = (selected + (forward ? 1 : count - 1)) % count;
    std::string error;
    if (!rerunSelected(error)) {
      overlayMessage_ = "Replay failed: " + error;
    }
    dirty_ = true;
    return;
  }
  if (key == GLFW_KEY_R || key == GLFW_KEY_ENTER || key == GLFW_KEY_F5) {
    std::string error;
    if (!rerunSelected(error)) {
      overlayMessage_ = "Replay failed: " + error;
    }
    dirty_ = true;
    return;
  }
  if (key >= GLFW_KEY_1 && key <= GLFW_KEY_4) {
    session_.state().viewMode = key - GLFW_KEY_1;
    std::string error;
    saveCheckpoint(error);
    overlayMessage_ = "View mode " + std::to_string(session_.state().viewMode);
    dirty_ = true;
    return;
  }
  if (key == GLFW_KEY_A) {
    runningAll_ = true;
    for (int index = 0;
         index < static_cast<int>(session_.state().runs.size()); ++index) {
      if (glfwWindowShouldClose(window_) != 0) {
        break;
      }
      std::string error;
      if (!rerunObject(index, true, error)) {
        overlayMessage_ = "All-object run failed: " + error;
      }
      dirty_ = true;
      presentFrame(error);
      glfwPollEvents();
    }
    runningAll_ = false;
    overlayMessage_ = "All objects replayed and exported";
    dirty_ = true;
  }
}

void Application::onMouseButton(int, int) {
  selectObjectAtCursor();
}

void Application::onCursor(double x, double y) {
  int windowWidth = 0;
  int windowHeight = 0;
  glfwGetWindowSize(window_, &windowWidth, &windowHeight);
  if (windowWidth <= 0 || windowHeight <= 0) {
    return;
  }
  const int mappedX = static_cast<int>(
    x * static_cast<double>(width_) / static_cast<double>(windowWidth));
  const int mappedY = static_cast<int>(
    y * static_cast<double>(height_) / static_cast<double>(windowHeight));
  session_.state().cursor = {
    std::max(0, std::min(width_ - 1, mappedX)),
    std::max(0, std::min(height_ - 1, mappedY)),
  };
  dirty_ = true;
}

void Application::selectObjectAtCursor() {
  const ScreenPoint cursor = session_.state().cursor;
  const std::uint32_t id = renderer_.pickObject(cursor.x, cursor.y);
  if (id == 0) {
    overlayMessage_ = "Picked background at " + std::to_string(cursor.x) +
                      "," + std::to_string(cursor.y);
    dirty_ = true;
    return;
  }
  const auto& objects = scene_.objects();
  for (std::size_t index = 0; index < objects.size(); ++index) {
    if (objects[index].id == id) {
      session_.state().selectedIndex = static_cast<int>(index);
      std::string error;
      if (!rerunSelected(error)) {
        overlayMessage_ = "Replay failed: " + error;
      } else {
        overlayMessage_ = "Picked " + objects[index].name;
      }
      dirty_ = true;
      return;
    }
  }
}


