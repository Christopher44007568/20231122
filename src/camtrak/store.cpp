#include "camtrak/store.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

#include "camtrak/model.hpp"
#include "camtrak/sha256.hpp"

namespace camtrak {
namespace fs = std::filesystem;

namespace {

JsonValue vec3ToJson(const Vec3& v) {
  JsonValue array = JsonValue::makeArray();
  array.push(JsonValue::makeNumber(v.x));
  array.push(JsonValue::makeNumber(v.y));
  array.push(JsonValue::makeNumber(v.z));
  return array;
}

Vec3 vec3FromJson(const JsonValue& value) {
  return {value.at(0).asNumber(), value.at(1).asNumber(),
          value.at(2).asNumber()};
}

JsonValue quatToJson(const Quat& q) {
  JsonValue array = JsonValue::makeArray();
  array.push(JsonValue::makeNumber(q.w));
  array.push(JsonValue::makeNumber(q.x));
  array.push(JsonValue::makeNumber(q.y));
  array.push(JsonValue::makeNumber(q.z));
  return array;
}

Quat quatFromJson(const JsonValue& value) {
  return {value.at(0).asNumber(), value.at(1).asNumber(),
          value.at(2).asNumber(), value.at(3).asNumber()};
}

JsonValue poseToJson(const Pose& pose) {
  JsonValue value = JsonValue::makeObject();
  value.set("position", vec3ToJson(pose.position));
  value.set("orientation", quatToJson(pose.orientation));
  return value;
}

Pose poseFromJson(const JsonValue& value) {
  return {vec3FromJson(value.get("position")),
          quatFromJson(value.get("orientation"))};
}

}  // namespace

JsonValue toJson(const Project& project) {
  JsonValue value = JsonValue::makeObject();
  value.set("schema_version", JsonValue::makeString("1.0"));
  value.set("project_id", JsonValue::makeString(project.project_id));
  value.set("created_at", JsonValue::makeString(project.created_at));
  value.set("description", JsonValue::makeString(project.description));
  JsonValue clocks = JsonValue::makeObject();
  JsonValue offsets = JsonValue::makeObject();
  JsonValue scales = JsonValue::makeObject();
  for (const auto& [device, offset] : project.clocks.offset_seconds) {
    offsets.set(device, JsonValue::makeNumber(offset));
  }
  for (const auto& [device, scale] : project.clocks.scale) {
    scales.set(device, JsonValue::makeNumber(scale));
  }
  clocks.set("offset_seconds", offsets);
  clocks.set("scale", scales);
  value.set("clocks", clocks);
  JsonValue objects = JsonValue::makeArray();
  for (const std::string& object_id : project.object_ids) {
    JsonValue entry = JsonValue::makeObject();
    entry.set("object_id", JsonValue::makeString(object_id));
    entry.set("input_file",
              JsonValue::makeString(project.input_files.at(object_id)));
    objects.push(entry);
  }
  value.set("objects", objects);
  return value;
}

Project projectFromJson(const JsonValue& value) {
  Project project;
  project.project_id = value.get("project_id").asString();
  project.created_at = value.get("created_at").asString();
  project.description = value.get("description").asString();
  const JsonValue& clocks = value.get("clocks");
  const JsonValue& offsets = clocks.get("offset_seconds");
  const JsonValue& scales = clocks.get("scale");
  for (const std::string& device : offsets.keys()) {
    project.clocks.offset_seconds[device] = offsets.get(device).asNumber();
  }
  for (const std::string& device : scales.keys()) {
    project.clocks.scale[device] = scales.get(device).asNumber();
  }
  const JsonValue& objects = value.get("objects");
  for (std::size_t i = 0; i < objects.size(); ++i) {
    const JsonValue& entry = objects.at(i);
    const std::string object_id = entry.get("object_id").asString();
    project.object_ids.push_back(object_id);
    project.input_files[object_id] = entry.get("input_file").asString();
  }
  return project;
}

JsonValue toJson(const Trajectory& trajectory) {
  JsonValue value = JsonValue::makeObject();
  value.set("schema_version", JsonValue::makeString("1.0"));
  value.set("object_id", JsonValue::makeString(trajectory.object_id));
  JsonValue samples = JsonValue::makeArray();
  for (const Sample& sample : trajectory.samples) {
    JsonValue entry = JsonValue::makeObject();
    entry.set("t_device", JsonValue::makeNumber(sample.t_device));
    entry.set("device_id", JsonValue::makeString(sample.device_id));
    entry.set("pose", poseToJson(sample.pose));
    samples.push(entry);
  }
  value.set("samples", samples);
  return value;
}

Trajectory trajectoryFromJson(const JsonValue& value) {
  Trajectory trajectory;
  trajectory.object_id = value.get("object_id").asString();
  const JsonValue& samples = value.get("samples");
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const JsonValue& entry = samples.at(i);
    Sample sample;
    sample.t_device = entry.get("t_device").asNumber();
    sample.device_id = entry.get("device_id").asString();
    sample.pose = poseFromJson(entry.get("pose"));
    trajectory.samples.push_back(sample);
  }
  return trajectory;
}

JsonValue toJson(const CorrectionConfig& config) {
  JsonValue value = JsonValue::makeObject();
  value.set("object_id", JsonValue::makeString(config.object_id));
  value.set("gap_t0", JsonValue::makeNumber(config.gap_t0));
  value.set("gap_t1", JsonValue::makeNumber(config.gap_t1));
  value.set("sample_period", JsonValue::makeNumber(config.sample_period));
  value.set("method",
            JsonValue::makeString(methodName(config.method)));
  return value;
}

CorrectionConfig configFromJson(const JsonValue& value) {
  CorrectionConfig config;
  config.object_id = value.get("object_id").asString();
  config.gap_t0 = value.get("gap_t0").asNumber();
  config.gap_t1 = value.get("gap_t1").asNumber();
  config.sample_period = value.get("sample_period").asNumber();
  config.method = parseMethod(value.get("method").asString());
  return config;
}

JsonValue toJson(const Metrics& metrics) {
  JsonValue value = JsonValue::makeObject();
  value.set("compared_samples",
            JsonValue::makeNumber(metrics.compared_samples));
  value.set("max_position_error",
            JsonValue::makeNumber(metrics.max_position_error));
  value.set("rmse_position", JsonValue::makeNumber(metrics.rmse_position));
  value.set("max_angle_error_rad",
            JsonValue::makeNumber(metrics.max_angle_error_rad));
  value.set("rmse_angle_rad",
            JsonValue::makeNumber(metrics.rmse_angle_rad));
  return value;
}

JsonValue toJson(const RunResult& result) {
  JsonValue value = JsonValue::makeObject();
  value.set("schema_version", JsonValue::makeString("1.0"));
  value.set("run_id", JsonValue::makeString(result.run_id));
  value.set("object_id", JsonValue::makeString(result.object_id));
  value.set("method", JsonValue::makeString(result.method));
  value.set("config", toJson(result.config));
  JsonValue segments = JsonValue::makeArray();
  for (const GapSegment& segment : result.corrected) {
    JsonValue entry = JsonValue::makeObject();
    entry.set("t", JsonValue::makeNumber(segment.t));
    entry.set("pose", poseToJson(segment.pose));
    segments.push(entry);
  }
  value.set("corrected", segments);
  value.set("metrics", toJson(result.metrics));
  return value;
}

RunResult resultFromJson(const JsonValue& value) {
  RunResult result;
  result.run_id = value.get("run_id").asString();
  result.object_id = value.get("object_id").asString();
  result.method = value.get("method").asString();
  result.config = configFromJson(value.get("config"));
  const JsonValue& segments = value.get("corrected");
  for (std::size_t i = 0; i < segments.size(); ++i) {
    const JsonValue& entry = segments.at(i);
    result.corrected.push_back(
        GapSegment{entry.get("t").asNumber(),
                   poseFromJson(entry.get("pose"))});
  }
  const JsonValue& metrics = value.get("metrics");
  result.metrics.compared_samples =
      static_cast<int>(metrics.get("compared_samples").asNumber());
  result.metrics.max_position_error =
      metrics.get("max_position_error").asNumber();
  result.metrics.rmse_position = metrics.get("rmse_position").asNumber();
  result.metrics.max_angle_error_rad =
      metrics.get("max_angle_error_rad").asNumber();
  result.metrics.rmse_angle_rad =
      metrics.get("rmse_angle_rad").asNumber();
  return result;
}

JsonValue toJson(const RunState& state) {
  JsonValue value = JsonValue::makeObject();
  value.set("run_id", JsonValue::makeString(state.run_id));
  value.set("object_id", JsonValue::makeString(state.object_id));
  value.set("status", JsonValue::makeString(state.status));
  value.set("started_at", JsonValue::makeString(state.started_at));
  value.set("updated_at", JsonValue::makeString(state.updated_at));
  value.set("config_hash", JsonValue::makeString(state.config_hash));
  value.set("config", toJson(state.config));
  value.set("last_error", JsonValue::makeString(state.last_error));
  return value;
}

RunState stateFromJson(const JsonValue& value) {
  RunState state;
  state.run_id = value.get("run_id").asString();
  state.object_id = value.get("object_id").asString();
  state.status = value.get("status").asString();
  state.started_at = value.get("started_at").asString();
  state.updated_at = value.get("updated_at").asString();
  state.config_hash = value.get("config_hash").asString();
  state.config = configFromJson(value.get("config"));
  state.last_error = value.get("last_error").asString();
  return state;
}

double ClockMapping::toReference(const std::string& device_id,
                                 double device_time) const {
  const double offset_value =
      offset_seconds.count(device_id) ? offset_seconds.at(device_id) : 0.0;
  const double scale_value =
      scale.count(device_id) ? scale.at(device_id) : 1.0;
  return device_time * scale_value + offset_value;
}

std::string readFile(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot read file: " + path);
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

void writeFileAtomic(const std::string& path, const std::string& contents) {
  fs::path target(path);
  fs::create_directories(target.parent_path());
  const fs::path temporary =
      target.parent_path() / (target.filename().string() + ".tmp");
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      throw std::runtime_error("cannot write file: " + temporary.string());
    }
    stream.write(contents.data(),
                 static_cast<std::streamsize>(contents.size()));
    stream.flush();
  }
  fs::rename(temporary, target);
}

std::string utcNowIso8601() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&time, &tm);
  std::ostringstream stream;
  stream << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

std::string makeRunId(const std::string& object_id) {
  static thread_local std::mt19937_64 engine(
      std::chrono::steady_clock::now().time_since_epoch().count());
  std::uniform_int_distribution<unsigned long long> dist;
  std::ostringstream stream;
  stream << "run_" << object_id << "_" << std::hex << std::setfill('0')
         << std::setw(16) << dist(engine);
  return stream.str();
}

Store::Store(std::string root) : root_(std::move(root)) {
  if (!fs::exists(fs::path(root_) / "project.json")) {
    throw std::runtime_error("no camtrak project at: " + root_);
  }
  for (const char* directory :
       {"inputs", "evidence/snapshots", "runs", "exports"}) {
    fs::create_directories(fs::path(root_) / directory);
  }
}

void Store::initialize(const std::string& root, const Project& project) {
  if (fs::exists(fs::path(root) / "project.json")) {
    throw std::runtime_error("project already exists at: " + root);
  }
  for (const char* directory :
       {"inputs", "evidence/snapshots", "runs", "exports"}) {
    fs::create_directories(fs::path(root) / directory);
  }
  writeFileAtomic((fs::path(root) / "project.json").string(),
                  toJson(project).dump());
  JsonValue manifest = JsonValue::makeObject();
  manifest.set("schema_version", JsonValue::makeString("1.0"));
  manifest.set("created_at", JsonValue::makeString(utcNowIso8601()));
  manifest.set("files", JsonValue::makeArray());
  writeFileAtomic((fs::path(root) / "evidence/manifest.json").string(),
                  manifest.dump());
  const std::string log_header =
      "{\"schema_version\":\"1.0\",\"note\":\"append-only run evidence "
      "log; entries are hash-chained\"}\n";
  writeFileAtomic((fs::path(root) / "evidence/runs.log.jsonl").string(),
                  log_header);
}

Project Store::loadProject() const {
  JsonParseError error;
  const JsonValue value =
      parseJson(readFile((fs::path(root_) / "project.json").string()), &error);
  if (value.isNull()) {
    throw std::runtime_error("malformed project.json: " + error.message);
  }
  return projectFromJson(value);
}

void Store::saveProject(const Project& project) const {
  writeFileAtomic((fs::path(root_) / "project.json").string(),
                  toJson(project).dump());
}

Trajectory Store::loadTrajectory(const std::string& object_id) const {
  const Project project = loadProject();
  auto found = project.input_files.find(object_id);
  if (found == project.input_files.end()) {
    throw std::runtime_error("unknown object id: " + object_id);
  }
  JsonParseError error;
  const JsonValue value = parseJson(
      readFile((fs::path(root_) / found->second).string()), &error);
  if (value.isNull()) {
    throw std::runtime_error("malformed trajectory for " + object_id + ": " +
                             error.message);
  }
  Trajectory trajectory = trajectoryFromJson(value);
  sortAndValidate(trajectory);
  return trajectory;
}

std::string Store::saveTrajectory(const std::string& object_id,
                                  const Trajectory& trajectory) const {
  Trajectory sorted = trajectory;
  sortAndValidate(sorted);
  const std::string relative = "inputs/" + object_id + ".json";
  const std::string serialized = toJson(sorted).dump();
  writeFileAtomic((fs::path(root_) / relative).string(), serialized);
  return sha256Hex(serialized);
}

Trajectory Store::toReferenceTimeline(const Trajectory& trajectory) const {
  const Project project = loadProject();
  Trajectory result = trajectory;
  for (Sample& sample : result.samples) {
    if (!project.clocks.offset_seconds.count(sample.device_id) &&
        !project.clocks.scale.count(sample.device_id)) {
      throw std::runtime_error("no clock mapping for device " +
                               sample.device_id);
    }
    sample.t_device =
        project.clocks.toReference(sample.device_id, sample.t_device);
  }
  sortAndValidate(result);
  return result;
}

std::string Store::snapshotInput(const std::string& object_id,
                                 const std::string& input_rel_path) const {
  const fs::path source = fs::path(root_) / input_rel_path;
  const std::string bytes = readFile(source.string());
  const std::string digest = sha256Hex(bytes);
  const fs::path snapshot_dir =
      fs::path(root_) / "evidence/snapshots" / digest;
  fs::create_directories(snapshot_dir);
  const fs::path destination = snapshot_dir / (object_id + ".json");
  if (!fs::exists(destination)) {
    writeFileAtomic(destination.string(), bytes);
  }
  return digest;
}

JsonValue Store::loadEvidenceManifest() const {
  JsonParseError error;
  const JsonValue value = parseJson(
      readFile((fs::path(root_) / "evidence/manifest.json").string()), &error);
  if (value.isNull()) {
    throw std::runtime_error("malformed evidence manifest: " + error.message);
  }
  return value;
}

void Store::rebuildEvidenceManifest() {
  const Project project = loadProject();
  JsonValue files = JsonValue::makeArray();
  for (const std::string& object_id : project.object_ids) {
    const std::string relative = project.input_files.at(object_id);
    const std::string digest = snapshotInput(object_id, relative);
    JsonValue entry = JsonValue::makeObject();
    entry.set("object_id", JsonValue::makeString(object_id));
    entry.set("input_file", JsonValue::makeString(relative));
    entry.set("sha256", JsonValue::makeString(digest));
    entry.set("snapshot",
              JsonValue::makeString("evidence/snapshots/" + digest + "/" +
                                    object_id + ".json"));
    files.push(entry);
  }
  JsonValue manifest = JsonValue::makeObject();
  manifest.set("schema_version", JsonValue::makeString("1.0"));
  manifest.set("updated_at", JsonValue::makeString(utcNowIso8601()));
  manifest.set("files", files);
  writeFileAtomic((fs::path(root_) / "evidence/manifest.json").string(),
                  manifest.dump());
}

bool Store::verifyEvidence(std::vector<std::string>* failures) const {
  if (failures != nullptr) failures->clear();
  bool ok = true;
  const JsonValue manifest = loadEvidenceManifest();
  const JsonValue& files = manifest.get("files");
  for (std::size_t i = 0; i < files.size(); ++i) {
    const JsonValue& entry = files.at(i);
    const std::string input_path = entry.get("input_file").asString();
    const std::string expected = entry.get("sha256").asString();
    const std::string snapshot_path = entry.get("snapshot").asString();
    const std::string input_bytes =
        readFile((fs::path(root_) / input_path).string());
    if (sha256Hex(input_bytes) != expected) {
      ok = false;
      if (failures != nullptr)
        failures->push_back("input hash mismatch for " + input_path);
    }
    const std::string snapshot_bytes =
        readFile((fs::path(root_) / snapshot_path).string());
    if (sha256Hex(snapshot_bytes) != expected) {
      ok = false;
      if (failures != nullptr)
        failures->push_back("snapshot hash mismatch for " + snapshot_path);
    }
  }
  return ok;
}

std::map<std::string, RunState> Store::loadSession() const {
  std::map<std::string, RunState> result;
  const fs::path path = fs::path(root_) / "session.json";
  if (!fs::exists(path)) return result;
  JsonParseError error;
  const JsonValue value = parseJson(readFile(path.string()), &error);
  if (value.isNull()) {
    throw std::runtime_error("malformed session.json: " + error.message);
  }
  const JsonValue& states = value.get("objects");
  for (std::size_t i = 0; i < states.size(); ++i) {
    const JsonValue& entry = states.at(i);
    RunState state = stateFromJson(entry);
    result[state.object_id] = state;
  }
  return result;
}

void Store::saveSessionState(const RunState& state) const {
  JsonValue root = JsonValue::makeObject();
  root.set("schema_version", JsonValue::makeString("1.0"));
  root.set("updated_at", JsonValue::makeString(utcNowIso8601()));
  JsonValue states = JsonValue::makeArray();
  std::map<std::string, RunState> existing = loadSession();
  existing[state.object_id] = state;
  for (const auto& [object_id, item] : existing) {
    states.push(toJson(item));
  }
  root.set("objects", states);
  writeFileAtomic((fs::path(root_) / "session.json").string(), root.dump());
}

void Store::clearSessionState(const std::string& object_id) const {
  std::map<std::string, RunState> existing = loadSession();
  existing.erase(object_id);
  JsonValue root = JsonValue::makeObject();
  root.set("schema_version", JsonValue::makeString("1.0"));
  root.set("updated_at", JsonValue::makeString(utcNowIso8601()));
  JsonValue states = JsonValue::makeArray();
  for (const auto& [id, item] : existing) {
    (void)id;
    states.push(toJson(item));
  }
  root.set("objects", states);
  writeFileAtomic((fs::path(root_) / "session.json").string(), root.dump());
}

void Store::saveRunResult(const RunResult& result) const {
  const fs::path path =
      fs::path(root_) / "runs" / (result.run_id + ".json");
  writeFileAtomic(path.string(), toJson(result).dump());
}

RunResult Store::loadRunResult(const std::string& run_id) const {
  JsonParseError error;
  const fs::path path =
      fs::path(root_) / "runs" / (run_id + ".json");
  const JsonValue value = parseJson(readFile(path.string()), &error);
  if (value.isNull()) {
    throw std::runtime_error("malformed run file " + run_id + ": " +
                             error.message);
  }
  return resultFromJson(value);
}

std::vector<std::string> Store::listRunIds() const {
  std::vector<std::string> result;
  for (const auto& entry : fs::directory_iterator(fs::path(root_) / "runs")) {
    if (entry.path().extension() == ".json") {
      result.push_back(entry.path().stem().string());
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

namespace {

std::string genesisHash() {
  return sha256Hex("camtrak-runs-log-v1");
}

JsonValue logEntryToJson(const RunLogEntry& entry) {
  JsonValue value = JsonValue::makeObject();
  value.set("schema_version", JsonValue::makeString("1.0"));
  value.set("run_id", JsonValue::makeString(entry.run_id));
  value.set("object_id", JsonValue::makeString(entry.object_id));
  value.set("status", JsonValue::makeString(entry.status));
  value.set("recorded_at", JsonValue::makeString(entry.recorded_at));
  value.set("config_hash", JsonValue::makeString(entry.config_hash));
  value.set("result_hash", JsonValue::makeString(entry.result_hash));
  value.set("prev_entry_hash",
            JsonValue::makeString(entry.prev_entry_hash));
  value.set("entry_hash", JsonValue::makeString(entry.entry_hash));
  return value;
}

RunLogEntry logEntryFromJson(const JsonValue& value) {
  RunLogEntry entry;
  entry.run_id = value.get("run_id").asString();
  entry.object_id = value.get("object_id").asString();
  entry.status = value.get("status").asString();
  entry.recorded_at = value.get("recorded_at").asString();
  entry.config_hash = value.get("config_hash").asString();
  entry.result_hash = value.get("result_hash").asString();
  entry.prev_entry_hash = value.get("prev_entry_hash").asString();
  entry.entry_hash = value.get("entry_hash").asString();
  return entry;
}

std::string computeEntryHash(JsonValue value) {
  // Serialize every field except the self hash to get a canonical payload.
  JsonValue payload = JsonValue::makeObject();
  for (const std::string& key : value.keys()) {
    if (key != "entry_hash") payload.set(key, value.get(key));
  }
  return sha256Hex(payload.dump());
}

}  // namespace

RunLogEntry Store::appendRunLog(const RunResult& result,
                                const std::string& status,
                                const std::string& config_hash) {
  const fs::path log_path =
      fs::path(root_) / "evidence/runs.log.jsonl";
  std::vector<RunLogEntry> previous = readRunLog();
  const std::string prev_hash =
      previous.empty() ? genesisHash() : previous.back().entry_hash;

  const std::string result_bytes = toJson(result).dump();
  RunLogEntry entry;
  entry.run_id = result.run_id;
  entry.object_id = result.object_id;
  entry.status = status;
  entry.recorded_at = utcNowIso8601();
  entry.config_hash = config_hash;
  entry.result_hash = sha256Hex(result_bytes);
  entry.prev_entry_hash = prev_hash;

  JsonValue entry_json = logEntryToJson(entry);
  entry.entry_hash = computeEntryHash(entry_json);
  entry_json.set("entry_hash", JsonValue::makeString(entry.entry_hash));

  std::ofstream stream(log_path, std::ios::binary | std::ios::app);
  if (!stream) {
    throw std::runtime_error("cannot open run log for append: " +
                             log_path.string());
  }
  stream << entry_json.dump();
  return entry;
}

std::vector<RunLogEntry> Store::readRunLog() const {
  std::vector<RunLogEntry> result;
  const fs::path log_path =
      fs::path(root_) / "evidence/runs.log.jsonl";
  const std::string contents = readFile(log_path.string());
  std::size_t position = 0;
  while (position < contents.size()) {
    const std::size_t start = contents.find('{', position);
    if (start == std::string::npos) break;
    // Entries are pretty-printed, so walk braces to find the full object.
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    std::size_t end = start;
    for (; end < contents.size(); ++end) {
      const char ch = contents[end];
      if (in_string) {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == '"') in_string = false;
        continue;
      }
      if (ch == '"') in_string = true;
      else if (ch == '{') ++depth;
      else if (ch == '}') {
        --depth;
        if (depth == 0) break;
      }
    }
    JsonParseError error;
    const JsonValue value =
        parseJson(contents.substr(start, end - start + 1), &error);
    position = end + 1;
    if (!value.isNull() && value.has("entry_hash")) {
      result.push_back(logEntryFromJson(value));
    }
  }
  return result;
}

bool Store::verifyRunLog(std::vector<std::string>* failures) const {
  if (failures != nullptr) failures->clear();
  bool ok = true;
  std::string expected_prev = genesisHash();
  const fs::path log_path =
      fs::path(root_) / "evidence/runs.log.jsonl";
  const std::string contents = readFile(log_path.string());
  for (const RunLogEntry& entry : readRunLog()) {
    // Re-parse the raw entry so the hash covers the on-disk JSON payload.
    JsonValue value = JsonValue::makeNull();
    const std::string marker = "\"run_id\": \"" + entry.run_id + "\"";
    const std::size_t hit = contents.find(marker);
    if (hit != std::string::npos) {
      const std::size_t start = contents.rfind('{', hit);
      int depth = 0;
      std::size_t end = start;
      for (; end < contents.size(); ++end) {
        if (contents[end] == '{') ++depth;
        if (contents[end] == '}') {
          --depth;
          if (depth == 0) break;
        }
      }
      JsonParseError error;
      value = parseJson(contents.substr(start, end - start + 1), &error);
    }
    if (entry.prev_entry_hash != expected_prev) {
      ok = false;
      if (failures != nullptr)
        failures->push_back("broken hash chain at run " + entry.run_id);
    }
    if (!value.isNull() &&
        computeEntryHash(value) != entry.entry_hash) {
      ok = false;
      if (failures != nullptr)
        failures->push_back("bad entry hash at run " + entry.run_id);
    }
    expected_prev = entry.entry_hash;
  }
  return ok;
}

std::string Store::createExport(const std::string& batch_id,
                                const std::vector<std::string>& run_ids,
                                const std::string& reviewer_note) const {
  if (batch_id.find_first_of("/\\") != std::string::npos) {
    throw std::runtime_error("invalid export batch id: " + batch_id);
  }
  const fs::path export_dir = fs::path(root_) / "exports" / batch_id;
  if (fs::exists(export_dir)) {
    throw std::runtime_error("export already exists: " + export_dir.string());
  }
  fs::create_directories(export_dir / "runs");
  fs::create_directories(export_dir / "snapshots");

  const Project project = loadProject();
  writeFileAtomic((export_dir / "project.json").string(),
                  toJson(project).dump());

  JsonValue run_records = JsonValue::makeArray();
  JsonValue file_hashes = JsonValue::makeArray();
  auto record_hash = [&](const std::string& relative,
                         const std::string& bytes) {
    JsonValue entry = JsonValue::makeObject();
    entry.set("path", JsonValue::makeString(relative));
    entry.set("sha256", JsonValue::makeString(sha256Hex(bytes)));
    file_hashes.push(entry);
  };

  for (const std::string& run_id : run_ids) {
    const RunResult result = loadRunResult(run_id);
    const std::string run_bytes = toJson(result).dump();
    const std::string relative = "runs/" + run_id + ".json";
    writeFileAtomic((export_dir / relative).string(), run_bytes);
    record_hash(relative, run_bytes);

    const std::string input_relative =
        project.input_files.at(result.object_id);
    const std::string digest =
        sha256Hex(readFile((fs::path(root_) / input_relative).string()));
    const fs::path snapshot_source =
        fs::path(root_) / "evidence/snapshots" / digest /
        (result.object_id + ".json");
    const std::string snapshot_bytes = readFile(snapshot_source.string());
    const std::string snapshot_relative =
        "snapshots/" + result.object_id + ".json";
    writeFileAtomic((export_dir / snapshot_relative).string(),
                    snapshot_bytes);
    record_hash(snapshot_relative, snapshot_bytes);

    JsonValue record = JsonValue::makeObject();
    record.set("run_id", JsonValue::makeString(run_id));
    record.set("object_id", JsonValue::makeString(result.object_id));
    record.set("method", JsonValue::makeString(result.method));
    record.set("input_snapshot_sha256",
               JsonValue::makeString(digest));
    record.set("run_file_sha256",
               JsonValue::makeString(sha256Hex(run_bytes)));
    run_records.push(record);
  }

  record_hash("project.json", toJson(project).dump());

  JsonValue manifest = JsonValue::makeObject();
  manifest.set("schema_version", JsonValue::makeString("1.0"));
  manifest.set("tool", JsonValue::makeString("camtrak"));
  manifest.set("batch_id", JsonValue::makeString(batch_id));
  manifest.set("exported_at", JsonValue::makeString(utcNowIso8601()));
  manifest.set("reviewer_note", JsonValue::makeString(reviewer_note));
  manifest.set("runs", run_records);
  manifest.set("file_hashes", file_hashes);
  const std::string manifest_bytes = manifest.dump();
  writeFileAtomic((export_dir / "export_manifest.json").string(),
                  manifest_bytes);

  const std::string readme =
      "camtrak export batch: " + batch_id +
      "\n\nReview steps:\n"
      "1. Recompute sha-256 for every path listed in export_manifest.json "
      "and compare.\n"
      "2. Open runs/<run_id>.json; the corrected samples and metrics are "
      "self-contained.\n"
      "3. Compare each run against the immutable input snapshot under "
      "snapshots/.\n"
      "4. Re-run `camtrak verify --project .` after copying this bundle into a "
      "project, or inspect the hashes manually.\n";
  writeFileAtomic((export_dir / "REVIEW.txt").string(), readme);
  return fs::absolute(export_dir).string();
}

}  // namespace camtrak
