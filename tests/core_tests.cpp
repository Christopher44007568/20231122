#include "compare.hpp"
#include "params.hpp"
#include "scene.hpp"
#include "session.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    ++failures;
  }
}

void setPixel(FrameData& frame, int x, int y, std::uint32_t objectId, float depth) {
  const std::size_t index = static_cast<std::size_t>(y) * frame.width + x;
  frame.aux[index] = {depth, static_cast<float>(objectId)};
  frame.rgba[index * 4] = objectId == 0 ? 16 : 220;
  frame.rgba[index * 4 + 1] = objectId == 0 ? 20 : 120;
  frame.rgba[index * 4 + 2] = objectId == 0 ? 24 : 60;
  frame.rgba[index * 4 + 3] = 255;
}

FrameData makeFrame(int width, int height) {
  FrameData frame;
  frame.width = width;
  frame.height = height;
  frame.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
  frame.aux.assign(static_cast<std::size_t>(width) * height, {1.0f, 0.0f});
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      setPixel(frame, x, y, 0, 1.0f);
    }
  }
  return frame;
}

void testComparison() {
  FrameData baseline = makeFrame(8, 6);
  FrameData current = makeFrame(8, 6);
  for (int y = 1; y <= 3; ++y) {
    for (int x = 2; x <= 4; ++x) {
      setPixel(baseline, x, y, 5, 2.0f);
      setPixel(current, x, y, x <= 2 ? 5 : 3, x <= 2 ? 2.0f : 1.5f);
    }
  }
  const CompareResult result = compareFrames(baseline, current, 5);
  check(result.focusPixels == 9, "focus pixel count");
  check(result.missingPixels == 6, "missing fragment count");
  check(result.addedPixels == 0, "added fragment count");
  check(result.classification == "fragment-gap", "fragment classification");
  check(result.expectedBounds.has_value(), "expected bounds present");
  check(result.boundary.size() == 4, "four boundary samples");
  if (result.boundary.size() == 4) {
    check(result.boundary[0].label == "left-outside", "left outside label");
    check(result.boundary[2].label == "right-inside", "right inside label");
    check(result.boundary[1].before.objectId == 5, "before boundary object");
    check(result.boundary[1].after.objectId == 3, "after boundary object");
    check(result.boundary[1].objectChanged, "boundary change recorded");
  }
  const FrameData diff = buildDiffFrame(baseline, current, 5);
  check(diff.width == 8 && diff.height == 6, "diff dimensions");
  const FrameData compare = buildSideBySideFrame(baseline, current);
  check(compare.width == 8 && compare.height == 6, "compare dimensions");
  check(compare.rgba[3] == 255, "compare image opaque");
}

void testPresets() {
  check(extremePresets().size() == kPresetCount, "preset count");
  check(presetAt(1).depthBiasConstant > 0.0f, "bias preset active");
  const auto candidates = correctionCandidates(presetAt(1));
  check(candidates.size() >= 6, "candidate count");
  check(candidates.front().constant == 0.0f, "zero-bias candidate first");
  check(!presetSummary(presetAt(0)).empty(), "preset summary");
}

void testSessionRoundTrip() {
  const Scene scene = Scene::makeDefault();
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "depth-recheck-session-test";
  std::error_code errorCode;
  std::filesystem::remove_all(root, errorCode);

  std::string error;
  SessionStore writer;
  check(writer.initialize(root, true, scene, error), "session initialize");
  check(writer.state().runs.size() == scene.objects().size(), "session runs");
  writer.state().selectedIndex = 2;
  writer.state().presetIndex = 3;
  writer.state().runs[2].extreme.valid = true;
  writer.state().runs[2].extreme.compare.missingPixels = 41;
  writer.state().runs[2].extreme.compare.classification = "fragment-gap";
  writer.state().runs[2].extreme.compare.boundary.push_back(
    {"left-inside", {14, 22}, {true, 3, 4.5f, 10, 20, 30},
     {true, 0, 0.0f, 2, 3, 4}, -4.5f, true, true});
  check(writer.save(error), "session save");

  SessionStore reader;
  check(reader.initialize(root, false, scene, error), "session resume");
  check(reader.state().resumed, "resume flag");
  check(reader.state().selectedIndex == 2, "selected index restored");
  check(reader.state().presetIndex == 3, "preset restored");
  check(reader.state().runs[2].extreme.compare.missingPixels == 41,
        "metrics restored");
  check(reader.state().runs[2].extreme.compare.boundary.size() == 1,
        "boundary evidence restored");
  check(reader.state().runs[2].extreme.compare.boundary[0].objectChanged,
        "boundary change restored");
  std::filesystem::remove_all(root, errorCode);
}

}  // namespace

int main() {
  testComparison();
  testPresets();
  testSessionRoundTrip();
  if (failures != 0) {
    std::cerr << failures << " core test(s) failed\n";
    return 1;
  }
  std::cout << "depth_core_tests: all checks passed\n";
  return 0;
}

