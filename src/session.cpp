#include "session.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string encodeFloat(float value) {
  std::ostringstream out;
  out << std::setprecision(9) << value;
  return out.str();
}

float decodeFloat(const std::string& value) {
  try {
    return std::stof(value);
  } catch (...) {
    return 0.0f;
  }
}

int decodeInt(const std::string& value) {
  try {
    return std::stoi(value);
  } catch (...) {
    return 0;
  }
}

std::vector<std::string> split(const std::string& line, char delimiter) {
  std::vector<std::string> fields;
  std::string field;
  std::istringstream input(line);
  while (std::getline(input, field, delimiter)) {
    fields.push_back(field);
  }
  return fields;
}

std::string boolText(bool value) { return value ? "1" : "0"; }

}  // namespace

std::string utcNow() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

bool SessionStore::initialize(const fs::path& root, bool fresh,
                              const Scene& scene, std::string& error) {
  root_ = root;
  std::error_code ec;
  fs::create_directories(root_, ec);
  if (ec) {
    error = "Cannot create session directory: " + ec.message();
    return false;
  }

  state_ = {};
  if (fresh) {
    fs::remove(root_ / "state.v1.tmp", ec);
    fs::remove_all(root_ / "evidence", ec);
  }
  if (!fresh && fs::exists(root_ / "state.v1")) {
    if (!load(error)) {
      return false;
    }
    state_.resumed = true;
  } else {
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
    state_.sessionId = "dr-" + std::to_string(stamp);
    state_.createdAt = utcNow();
    state_.updatedAt = state_.createdAt;
    state_.note = "New checkpoint";
  }

  if (state_.runs.empty()) {
    for (const RenderObject& object : scene.objects()) {
      ObjectRun run;
      run.id = object.id;
      run.name = object.name;
      state_.runs.push_back(std::move(run));
    }
  } else {
    for (std::size_t index = 0; index < state_.runs.size(); ++index) {
      if (index < scene.objects().size()) {
        state_.runs[index].id = scene.objects()[index].id;
        if (state_.runs[index].name.empty()) {
          state_.runs[index].name = scene.objects()[index].name;
        }
      }
      if (state_.runs[index].evidenceDir.empty()) {
        state_.runs[index].evidenceDir =
          objectEvidenceDir(static_cast<int>(index)).generic_string();
      }
      state_.runs[index].evidenceComplete = evidenceFilesPresent(state_.runs[index]);
    }
  }
  state_.selectedIndex = std::max(0, std::min(
    static_cast<int>(state_.runs.size()) - 1, state_.selectedIndex));
  return save(error);
}

fs::path SessionStore::objectEvidenceDir(int index) const {
  if (index < 0 || index >= static_cast<int>(state_.runs.size())) {
    return root_ / "evidence" / "invalid";
  }
  std::ostringstream name;
  name << std::setw(2) << std::setfill('0') << state_.runs[index].id << "-"
       << state_.runs[index].name;
  return root_ / "evidence" / name.str();
}

fs::path SessionStore::baselineEvidencePath() const {
  return root_ / "evidence" / "baseline.png";
}

bool SessionStore::evidenceFilesPresent(const ObjectRun& run) const {
  if (run.evidenceDir.empty()) {
    return false;
  }
  const fs::path dir(run.evidenceDir);
  return fs::exists(dir / "extreme.png") &&
         fs::exists(dir / "corrected.png") &&
         fs::exists(dir / "diff.png") &&
         fs::exists(dir / "corrected_diff.png") &&
         fs::exists(dir / "compare.png") &&
         fs::exists(dir / "report.json") &&
         fs::exists(dir / "boundary.csv") &&
         fs::exists(dir / "candidates.csv");
}

bool SessionStore::save(std::string& error) {
  state_.updatedAt = utcNow();
  const fs::path finalPath = root_ / "state.v1";
  const fs::path temporaryPath = root_ / "state.v1.tmp";
  std::error_code ec;
  fs::create_directories(root_, ec);
  if (ec) {
    error = "Cannot create checkpoint directory: " + ec.message();
    return false;
  }

  std::ofstream out(temporaryPath, std::ios::trunc);
  if (!out) {
    error = "Cannot write checkpoint: " + temporaryPath.string();
    return false;
  }
  out << "meta|1|" << state_.sessionId << "|" << state_.createdAt << "|"
      << state_.updatedAt << "|" << state_.selectedIndex << "|"
      << state_.presetIndex << "|" << state_.viewMode << "|"
      << state_.cursor.x << "|" << state_.cursor.y << "|" << state_.note << "\n";

  for (std::size_t index = 0; index < state_.runs.size(); ++index) {
    const ObjectRun& run = state_.runs[index];
    out << "run|" << run.id << "|" << run.name << "|" << run.evidenceDir << "|"
        << boolText(run.evidenceComplete) << "|" << boolText(run.extreme.valid)
        << "|" << encodeFloat(run.extreme.bias.constant)
        << "|" << encodeFloat(run.extreme.bias.slope)
        << "|" << encodeFloat(static_cast<float>(run.extreme.score))
        << "|" << encodeFloat(static_cast<float>(run.extreme.elapsedMs))
        << "|" << run.extreme.compare.focusPixels
        << "|" << run.extreme.compare.missingPixels
        << "|" << run.extreme.compare.addedPixels
        << "|" << run.extreme.compare.changedPixels
        << "|" << encodeFloat(run.extreme.compare.meanAbsDepthDelta)
        << "|" << boolText(run.extreme.compare.expectedBounds.has_value());
    const RectI empty{};
    const RectI expected = run.extreme.compare.expectedBounds.value_or(empty);
    const RectI changed = run.extreme.compare.changeBounds.value_or(empty);
    out << "|" << expected.x << "|" << expected.y << "|" << expected.width
        << "|" << expected.height
        << "|" << boolText(run.extreme.compare.changeBounds.has_value())
        << "|" << changed.x << "|" << changed.y << "|" << changed.width
        << "|" << changed.height << "|"
        << (run.extreme.compare.classification.empty()
              ? "-" : run.extreme.compare.classification);
    out << "|" << boolText(run.corrected.valid)
        << "|" << encodeFloat(run.corrected.bias.constant)
        << "|" << encodeFloat(run.corrected.bias.slope)
        << "|" << encodeFloat(static_cast<float>(run.corrected.score))
        << "|" << encodeFloat(static_cast<float>(run.corrected.elapsedMs))
        << "|" << run.corrected.compare.focusPixels
        << "|" << run.corrected.compare.missingPixels
        << "|" << run.corrected.compare.addedPixels
        << "|" << run.corrected.compare.changedPixels
        << "|" << encodeFloat(run.corrected.compare.meanAbsDepthDelta)
        << "|" << boolText(run.corrected.compare.expectedBounds.has_value());
    const RectI correctedExpected = run.corrected.compare.expectedBounds.value_or(empty);
    const RectI correctedChanged = run.corrected.compare.changeBounds.value_or(empty);
    out << "|" << correctedExpected.x << "|" << correctedExpected.y
        << "|" << correctedExpected.width << "|" << correctedExpected.height
        << "|" << boolText(run.corrected.compare.changeBounds.has_value())
        << "|" << correctedChanged.x << "|" << correctedChanged.y
        << "|" << correctedChanged.width << "|" << correctedChanged.height
        << "|" << (run.corrected.compare.classification.empty()
                     ? "-" : run.corrected.compare.classification)
        << "\n";

    const auto writeBoundary = [&](const char* phase, const CompareResult& compare) {
      for (std::size_t sampleIndex = 0; sampleIndex < compare.boundary.size();
           ++sampleIndex) {
        const BoundarySample& sample = compare.boundary[sampleIndex];
        out << "boundary|" << index << "|" << phase << "|" << sampleIndex << "|"
            << sample.label << "|" << sample.point.x << "|" << sample.point.y
            << "|" << boolText(sample.before.valid)
            << "|" << sample.before.objectId << "|"
            << encodeFloat(sample.before.depth)
            << "|" << boolText(sample.after.valid)
            << "|" << sample.after.objectId << "|"
            << encodeFloat(sample.after.depth)
            << "|" << encodeFloat(sample.depthDelta)
            << "|" << boolText(sample.objectChanged)
            << "|" << boolText(sample.colorChanged) << "\n";
      }
    };
    writeBoundary("extreme", run.extreme.compare);
    writeBoundary("corrected", run.corrected.compare);
  }
  out.flush();
  if (!out) {
    error = "Checkpoint write failed";
    return false;
  }
  out.close();
  fs::rename(temporaryPath, finalPath, ec);
  if (ec) {
    error = "Cannot commit checkpoint: " + ec.message();
    return false;
  }
  return true;
}

bool SessionStore::load(std::string& error) {
  std::ifstream in(root_ / "state.v1");
  if (!in) {
    error = "Cannot read session checkpoint";
    return false;
  }
  SessionState loaded;
  bool sawMeta = false;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::vector<std::string> fields = split(line, '|');
    if (fields.empty()) {
      continue;
    }
    if (fields[0] == "meta" && fields.size() >= 11) {
      if (fields[1] != "1") {
        error = "Unsupported checkpoint version";
        return false;
      }
      loaded.sessionId = fields[2];
      loaded.createdAt = fields[3];
      loaded.updatedAt = fields[4];
      loaded.selectedIndex = decodeInt(fields[5]);
      loaded.presetIndex = decodeInt(fields[6]);
      loaded.viewMode = decodeInt(fields[7]);
      loaded.cursor = {decodeInt(fields[8]), decodeInt(fields[9])};
      loaded.note = fields[10];
      sawMeta = true;
      continue;
    }
    if (fields[0] == "run" && fields.size() >= 47) {
      ObjectRun run;
      run.id = static_cast<std::uint32_t>(decodeInt(fields[1]));
      run.name = fields[2];
      run.evidenceDir = fields[3];
      run.evidenceComplete = decodeInt(fields[4]) != 0;
      run.extreme.valid = decodeInt(fields[5]) != 0;
      run.extreme.bias = {decodeFloat(fields[6]), decodeFloat(fields[7])};
      run.extreme.score = decodeFloat(fields[8]);
      run.extreme.elapsedMs = decodeFloat(fields[9]);
      run.extreme.compare.focusId = run.id;
      run.extreme.compare.focusPixels = decodeInt(fields[10]);
      run.extreme.compare.missingPixels = decodeInt(fields[11]);
      run.extreme.compare.addedPixels = decodeInt(fields[12]);
      run.extreme.compare.changedPixels = decodeInt(fields[13]);
      run.extreme.compare.meanAbsDepthDelta = decodeFloat(fields[14]);
      if (decodeInt(fields[15]) != 0) {
        run.extreme.compare.expectedBounds =
          RectI{decodeInt(fields[16]), decodeInt(fields[17]),
                decodeInt(fields[18]), decodeInt(fields[19])};
      }
      if (decodeInt(fields[20]) != 0) {
        run.extreme.compare.changeBounds =
          RectI{decodeInt(fields[21]), decodeInt(fields[22]),
                decodeInt(fields[23]), decodeInt(fields[24])};
      }
      if (fields[25] != "-") {
        run.extreme.compare.classification = fields[25];
      }
      run.corrected.valid = decodeInt(fields[26]) != 0;
      run.corrected.bias = {decodeFloat(fields[27]), decodeFloat(fields[28])};
      run.corrected.score = decodeFloat(fields[29]);
      run.corrected.elapsedMs = decodeFloat(fields[30]);
      run.corrected.compare.focusId = run.id;
      run.corrected.compare.focusPixels = decodeInt(fields[31]);
      run.corrected.compare.missingPixels = decodeInt(fields[32]);
      run.corrected.compare.addedPixels = decodeInt(fields[33]);
      run.corrected.compare.changedPixels = decodeInt(fields[34]);
      run.corrected.compare.meanAbsDepthDelta = decodeFloat(fields[35]);
      if (decodeInt(fields[36]) != 0) {
        run.corrected.compare.expectedBounds =
          RectI{decodeInt(fields[37]), decodeInt(fields[38]),
                decodeInt(fields[39]), decodeInt(fields[40])};
      }
      if (decodeInt(fields[41]) != 0) {
        run.corrected.compare.changeBounds =
          RectI{decodeInt(fields[42]), decodeInt(fields[43]),
                decodeInt(fields[44]), decodeInt(fields[45])};
      }
      if (fields[46] != "-") {
        run.corrected.compare.classification = fields[46];
      }
      run.hasCorrected = run.corrected.valid;
      loaded.runs.push_back(std::move(run));
      continue;
    }
    if (fields[0] == "boundary" && fields.size() >= 16) {
      const int runIndex = decodeInt(fields[1]);
      if (runIndex < 0 || runIndex >= static_cast<int>(loaded.runs.size())) {
        continue;
      }
      CompareResult* compare = nullptr;
      if (fields[2] == "extreme") {
        compare = &loaded.runs[runIndex].extreme.compare;
      } else if (fields[2] == "corrected") {
        compare = &loaded.runs[runIndex].corrected.compare;
      }
      if (compare == nullptr) {
        continue;
      }
      const std::size_t sampleIndex =
        static_cast<std::size_t>(std::max(0, decodeInt(fields[3])));
      if (compare->boundary.size() <= sampleIndex) {
        compare->boundary.resize(sampleIndex + 1);
      }
      BoundarySample& sample = compare->boundary[sampleIndex];
      sample.label = fields[4];
      sample.point = {decodeInt(fields[5]), decodeInt(fields[6])};
      sample.before.valid = decodeInt(fields[7]) != 0;
      sample.before.objectId = static_cast<std::uint32_t>(decodeInt(fields[8]));
      sample.before.depth = decodeFloat(fields[9]);
      sample.after.valid = decodeInt(fields[10]) != 0;
      sample.after.objectId = static_cast<std::uint32_t>(decodeInt(fields[11]));
      sample.after.depth = decodeFloat(fields[12]);
      sample.depthDelta = decodeFloat(fields[13]);
      sample.objectChanged = decodeInt(fields[14]) != 0;
      sample.colorChanged = decodeInt(fields[15]) != 0;
    }
  }
  if (!sawMeta) {
    error = "Checkpoint has no metadata";
    return false;
  }
  state_ = std::move(loaded);
  return true;
}


