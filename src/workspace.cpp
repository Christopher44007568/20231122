#include "gbi/workspace.hpp"

#include "gbi/renderer.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/file.h>
#include <unistd.h>

namespace gbi {

namespace {

std::string flatten(const std::string& value) {
  std::string output = value;
  for (char& character : output) {
    if (character == '\n' || character == '\r') {
      character = ' ';
    }
  }
  return output;
}

std::map<std::string, std::string> parseKeyValues(const std::string& text) {
  std::map<std::string, std::string> values;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const std::size_t separator = line.find('=');
    if (separator == std::string::npos) {
      continue;
    }
    values[trim(line.substr(0, separator))] = trim(line.substr(separator + 1));
  }
  return values;
}

std::string serializeKeyValues(const std::map<std::string, std::string>& values) {
  std::ostringstream stream;
  for (const auto& entry : values) {
    stream << entry.first << '=' << flatten(entry.second) << '\n';
  }
  return stream.str();
}

std::string compactStamp(const TimeStamp& stamp) {
  std::string value = stamp.utc;
  for (char& character : value) {
    if (character == '-' || character == ':' || character == '.') {
      character = '_';
    }
  }
  return value;
}

std::string relativeToRoot(const fs::path& root, const fs::path& path) {
  std::error_code error;
  const fs::path relative = fs::relative(path, root, error);
  if (error) {
    return path.string();
  }
  return relative.generic_string();
}

void writePartialEvidence(const fs::path& staging, const std::string& stage,
                          const std::string& error) {
  if (staging.empty()) {
    return;
  }
  ensureDir(staging);
  const TimeStamp stamp = nowStamp();
  std::ostringstream text;
  text << "capture interrupted\n";
  text << "stage=" << stage << "\n";
  text << "utc=" << stamp.utc << "\n";
  text << "unix_ms=" << stamp.unix_ms << "\n";
  text << "error=" << flatten(error) << "\n";
  atomicWriteText(staging / "INTERRUPTED.txt", text.str());
}

}  // namespace

std::string SessionState::get(const std::string& key,
                              const std::string& fallback) const {
  const auto iterator = values.find(key);
  return iterator == values.end() ? fallback : iterator->second;
}

void SessionState::set(const std::string& key, const std::string& value) {
  values[key] = value;
}

std::uint64_t SessionState::number(const std::string& key,
                                   std::uint64_t fallback) const {
  const std::string value = get(key);
  if (value.empty()) {
    return fallback;
  }
  try {
    return std::stoull(value);
  } catch (...) {
    return fallback;
  }
}

std::string JobRecord::toText() const {
  std::map<std::string, std::string> values;
  values["job_id"] = job_id;
  values["status"] = status;
  values["operation"] = operation;
  values["anchor"] = anchor;
  values["object_id"] = object_id;
  values["capture_id"] = capture_id;
  values["revision"] = revision;
  values["note"] = note;
  values["started_utc"] = started_utc;
  values["started_unix_ms"] = std::to_string(started_unix_ms);
  values["width"] = std::to_string(width);
  values["height"] = std::to_string(height);
  values["staging_dir"] = staging_dir;
  values["final_dir"] = final_dir;
  values["error"] = error;
  return serializeKeyValues(values);
}

JobRecord JobRecord::fromText(const std::string& text) {
  const auto values = parseKeyValues(text);
  JobRecord job;
  const auto get = [&](const std::string& key) {
    const auto iterator = values.find(key);
    return iterator == values.end() ? std::string() : iterator->second;
  };
  job.job_id = get("job_id");
  job.status = get("status");
  job.operation = get("operation");
  job.anchor = get("anchor");
  job.object_id = get("object_id");
  job.capture_id = get("capture_id");
  job.revision = get("revision");
  job.note = get("note");
  job.started_utc = get("started_utc");
  if (!get("started_unix_ms").empty()) {
    job.started_unix_ms = std::stoll(get("started_unix_ms"));
  }
  if (!get("width").empty()) {
    job.width = static_cast<std::uint32_t>(std::stoul(get("width")));
  }
  if (!get("height").empty()) {
    job.height = static_cast<std::uint32_t>(std::stoul(get("height")));
  }
  job.staging_dir = get("staging_dir");
  job.final_dir = get("final_dir");
  job.error = get("error");
  return job;
}

std::string OriginalManifest::toJson() const {
  Json root = Json::object();
  root["schema"] = "gbi.original/1";
  root["source_path"] = source_path;
  root["imported_path"] = imported_path;
  root["sha256"] = sha256;
  root["size"] = size;
  root["imported_at"] = imported_at;
  return root.dump(2);
}

OriginalManifest OriginalManifest::fromJson(const Json& json) {
  OriginalManifest manifest;
  manifest.source_path = json.value("source_path");
  manifest.imported_path = json.value("imported_path");
  manifest.sha256 = json.value("sha256");
  manifest.size = json.u64("size");
  manifest.imported_at = json.value("imported_at");
  return manifest;
}

Workspace::Workspace(fs::path root)
    : root_(fs::absolute(std::move(root)).lexically_normal()) {}

const fs::path& Workspace::root() const {
  return root_;
}

const fs::path& Workspace::originalsDir() const {
  static const fs::path segment = "originals";
  static thread_local fs::path path;
  path = root_ / segment;
  return path;
}

const fs::path& Workspace::stateDir() const {
  static const fs::path segment = "state";
  static thread_local fs::path path;
  path = root_ / segment;
  return path;
}

const fs::path& Workspace::capturesDir() const {
  static const fs::path segment = "captures";
  static thread_local fs::path path;
  path = root_ / segment;
  return path;
}

const fs::path& Workspace::comparisonsDir() const {
  static const fs::path segment = "comparisons";
  static thread_local fs::path path;
  path = root_ / segment;
  return path;
}

const fs::path& Workspace::exportsDir() const {
  static const fs::path segment = "exports";
  static thread_local fs::path path;
  path = root_ / segment;
  return path;
}

const fs::path& Workspace::journalPath() const {
  static thread_local fs::path path;
  path = stateDir() / "journal.jsonl";
  return path;
}

const fs::path& Workspace::sessionPath() const {
  static thread_local fs::path path;
  path = stateDir() / "session.ini";
  return path;
}

const fs::path& Workspace::originalManifestPath() const {
  static thread_local fs::path path;
  path = stateDir() / "original.json";
  return path;
}

const fs::path& Workspace::originalScenePath() const {
  static thread_local fs::path path;
  path = originalsDir() / "scene.json";
  return path;
}

bool Workspace::exists() const {
  std::error_code error;
  return fs::is_regular_file(sessionPath(), error) &&
         fs::is_regular_file(originalScenePath(), error);
}

void Workspace::require() const {
  if (!exists()) {
    throw std::runtime_error(
        "workspace is not initialized; run `gbuffer-inspector init` first: " +
        root_.string());
  }
}

void Workspace::ensureLayout() const {
  ensureDir(root_);
  ensureDir(originalsDir());
  ensureDir(stateDir());
  ensureDir(stateDir() / "jobs");
  ensureDir(stateDir() / "overlays");
  ensureDir(capturesDir());
  ensureDir(comparisonsDir());
  ensureDir(exportsDir());
}

Workspace Workspace::initialize(const fs::path& root,
                                const fs::path& original_asset, bool force) {
  Workspace workspace(root);
  if (force) {
    std::error_code error;
    fs::remove_all(workspace.root_, error);
    if (error) {
      throw std::runtime_error("cannot reset workspace: " +
                               error.message());
    }
  } else if (workspace.exists()) {
    return workspace;
  }
  if (!fs::is_regular_file(original_asset)) {
    throw std::runtime_error("original material not found: " +
                             original_asset.string());
  }
  workspace.ensureLayout();
  if (!fs::exists(workspace.originalScenePath())) {
    const Bytes source = readFile(original_asset);
    writeFile(workspace.originalScenePath(), source);
  }
  makeReadOnly(workspace.originalScenePath());
  OriginalManifest manifest;
  manifest.source_path = fs::absolute(original_asset).string();
  manifest.imported_path = relativeToRoot(workspace.root_,
                                          workspace.originalScenePath());
  manifest.sha256 = sha256File(workspace.originalScenePath());
  manifest.size = fs::file_size(workspace.originalScenePath());
  manifest.imported_at = nowStamp().utc;
  workspace.writeOriginalManifest(manifest);
  if (!workspace.verifyOriginals(false)) {
    throw std::runtime_error("imported original material failed verification");
  }

  if (!fs::exists(workspace.sessionPath())) {
    const Scene scene = Scene::load(workspace.originalScenePath());
    const TimeStamp stamp = nowStamp();
    SessionState session;
    session.set("schema", "gbi.session/1");
    session.set("workspace_id", randomToken(8));
    session.set("created_utc", stamp.utc);
    session.set("updated_utc", stamp.utc);
    session.set("generation", "1");
    session.set("selected_object",
                scene.objects.empty() ? "" : scene.objects.front().id);
    session.set("selected_anchor", "A0");
    session.set("clean_exit", "1");
    session.set("pending_job", "");
    session.set("last_action", "init");
    session.set("original_sha256", manifest.sha256);
    workspace.saveSession(session);
  }
  Json journal_fields = Json::object();
  journal_fields["workspace"] = workspace.root_.string();
  journal_fields["original_sha256"] = manifest.sha256;
  journal_fields["original_source"] = manifest.source_path;
  workspace.journal("workspace.init", journal_fields);
  return workspace;
}

Workspace Workspace::open(const fs::path& root) {
  Workspace workspace(root);
  workspace.require();
  return workspace;
}

Workspace Workspace::ensure(const fs::path& root) {
  Workspace workspace(root);
  if (workspace.exists()) {
    return workspace;
  }
  return initialize(root, fs::path(GBI_SOURCE_DIR) / "assets" /
                              "original_scene.json",
                    false);
}

Scene Workspace::loadScene(bool apply_overlays) const {
  require();
  Scene scene = Scene::load(originalScenePath());
  if (apply_overlays) {
    loadOverlays(scene);
  }
  return scene;
}

void Workspace::loadOverlays(Scene& scene) const {
  const fs::path directory = stateDir() / "overlays";
  std::error_code error;
  if (!fs::is_directory(directory, error)) {
    return;
  }
  for (const fs::directory_entry& entry :
       fs::directory_iterator(directory, error)) {
    if (error) {
      throw std::runtime_error("cannot scan overlays: " + error.message());
    }
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    const std::string object_id = entry.path().stem().string();
    if (scene.find(object_id) == nullptr) {
      continue;
    }
    const std::string text = readText(entry.path());
    const Json overlay = Json::parse(text);
    scene.applyOverlay(
        object_id, overlay,
        entry.path().lexically_relative(root_).generic_string(), text);
  }
}

fs::path Workspace::overlayPath(const std::string& object_id) const {
  return stateDir() / "overlays" / (sanitizeName(object_id) + ".json");
}

void Workspace::writeOverlay(const ObjectOverlay& overlay) {
  atomicWriteText(overlayPath(overlay.object_id),
                  overlayToJson(overlay).dump(2));
}

bool Workspace::clearOverlay(const std::string& object_id) {
  std::error_code error;
  return fs::remove(overlayPath(object_id), error);
}

std::optional<ObjectOverlay> Workspace::readOverlay(
    const std::string& object_id) const {
  const fs::path path = overlayPath(object_id);
  std::error_code error;
  if (!fs::is_regular_file(path, error)) {
    return std::nullopt;
  }
  return parseOverlay(object_id, path);
}

SessionState Workspace::loadSession() const {
  require();
  SessionState session;
  session.values = parseKeyValues(readText(sessionPath()));
  return session;
}

void Workspace::saveSession(const SessionState& session) {
  SessionState copy = session;
  copy.set("updated_utc", nowStamp().utc);
  atomicWriteText(sessionPath(), serializeKeyValues(copy.values));
}

void Workspace::mutateSession(
    const std::function<void(SessionState&)>& mutation) {
  SessionState session = loadSession();
  mutation(session);
  saveSession(session);
}

std::uint64_t Workspace::nextGeneration() {
  std::uint64_t generation = 0;
  mutateSession([&](SessionState& session) {
    generation = session.number("generation", 1) + 1;
    session.set("generation", std::to_string(generation));
  });
  return generation;
}

OriginalManifest Workspace::loadOriginalManifest() const {
  return OriginalManifest::fromJson(
      Json::parse(readText(originalManifestPath())));
}

void Workspace::writeOriginalManifest(
    const OriginalManifest& manifest) const {
  atomicWriteText(originalManifestPath(), manifest.toJson());
}

bool Workspace::verifyOriginals(bool repair_permissions) const {
  std::error_code error;
  if (!fs::is_regular_file(originalScenePath(), error) ||
      !fs::is_regular_file(originalManifestPath(), error)) {
    return false;
  }
  const OriginalManifest manifest = loadOriginalManifest();
  const std::uint64_t size = fs::file_size(originalScenePath(), error);
  if (error || size != manifest.size) {
    return false;
  }
  if (sha256File(originalScenePath()) != manifest.sha256) {
    return false;
  }
  const fs::perms permissions = fs::status(originalScenePath()).permissions();
  const fs::perms write_bits =
      fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
  if ((permissions & write_bits) != fs::perms::none) {
    if (!repair_permissions) {
      return false;
    }
    makeReadOnly(originalScenePath());
    if ((fs::status(originalScenePath()).permissions() & write_bits) !=
        fs::perms::none) {
      return false;
    }
  }
  if (!manifest.source_path.empty() &&
      fs::is_regular_file(manifest.source_path, error)) {
    if (sha256File(manifest.source_path) != manifest.sha256) {
      return false;
    }
  }
  return true;
}

void Workspace::recordOriginalsVerified() {
  mutateSession([&](SessionState& session) {
    session.set("originals_verified_utc", nowStamp().utc);
    session.set("originals_sha256", loadOriginalManifest().sha256);
  });
}

JobRecord Workspace::beginJob(
    const std::string& operation, const std::string& anchor,
    const std::string& object_id, const std::string& capture_id,
    const std::string& revision, const std::string& note,
    std::uint32_t width, std::uint32_t height) {
  const TimeStamp stamp = nowStamp();
  JobRecord job;
  job.job_id = compactStamp(stamp) + "-" + randomToken(4);
  job.status = "staging";
  job.operation = operation;
  job.anchor = anchor;
  job.object_id = object_id;
  job.capture_id = capture_id;
  job.revision = revision;
  job.note = note;
  job.started_utc = stamp.utc;
  job.started_unix_ms = stamp.unix_ms;
  job.width = width;
  job.height = height;
  job.staging_dir =
      (capturesDir() / (".staging-" + job.job_id)).generic_string();
  job.final_dir =
      (capturesDir() / anchor / capture_id).generic_string();
  ensureDir(job.staging_dir);
  ensureDir(fs::path(job.final_dir).parent_path());
  atomicWriteText(stateDir() / "jobs" / (job.job_id + ".job"), job.toText());
  mutateSession([&](SessionState& session) {
    session.set("pending_job", job.job_id);
    session.set("clean_exit", "0");
    session.set("last_action", operation + ".begin");
  });
  Json fields = Json::object();
  fields["job_id"] = job.job_id;
  fields["capture_id"] = capture_id;
  fields["anchor"] = anchor;
  fields["object_id"] = object_id;
  fields["staging_dir"] = job.staging_dir;
  fields["final_dir"] = job.final_dir;
  journal(operation + ".begin", fields);
  return job;
}

void Workspace::updateJob(const JobRecord& job) {
  atomicWriteText(stateDir() / "jobs" / (job.job_id + ".job"), job.toText());
}

void Workspace::completeJob(const JobRecord& job) {
  JobRecord updated = job;
  const fs::path staging(job.staging_dir);
  const fs::path final_path(job.final_dir);
  ensureDir(final_path.parent_path());
  std::error_code error;
  if (fs::exists(final_path, error)) {
    throw std::runtime_error("capture destination already exists: " +
                             final_path.string());
  }
  if (!fs::is_regular_file(staging / "capture.json", error)) {
    throw std::runtime_error("staging capture manifest is missing: " +
                             staging.string());
  }
  const CaptureEvidence capture =
      CaptureEvidence::fromJson(Json::parse(readText(staging / "capture.json")));
  updated.status = "committing";
  updateJob(updated);
  fs::rename(staging, final_path, error);
  if (error) {
    throw std::runtime_error("cannot commit capture directory: " +
                             error.message());
  }
  fsyncDirectory(final_path);
  fsyncDirectory(capturesDir());
  recordCapture(capture, final_path);
  updated.status = "complete";
  updateJob(updated);
  mutateSession([&](SessionState& session) {
    session.set("pending_job", "");
    session.set("clean_exit", "1");
    session.set("last_action", job.operation + ".complete");
    session.set("last_capture_id", capture.capture_id);
  });
  Json fields = Json::object();
  fields["job_id"] = job.job_id;
  fields["capture_id"] = capture.capture_id;
  fields["anchor"] = capture.anchor;
  fields["capture_dir"] = relativeToRoot(root_, final_path);
  fields["started_utc"] = capture.started_at.utc;
  fields["completed_utc"] = capture.completed_at.utc;
  journal(job.operation + ".complete", fields);
}

void Workspace::failJob(const JobRecord& job, const std::string& error_text) {
  JobRecord updated = job;
  updated.status = "interrupted";
  updated.error = error_text;
  updateJob(updated);
  writePartialEvidence(fs::path(job.staging_dir), "capture", error_text);
  mutateSession([&](SessionState& session) {
    session.set("pending_job", job.job_id);
    session.set("clean_exit", "0");
    session.set("last_action", job.operation + ".interrupted");
  });
  Json fields = Json::object();
  fields["job_id"] = job.job_id;
  fields["capture_id"] = job.capture_id;
  fields["anchor"] = job.anchor;
  fields["object_id"] = job.object_id;
  fields["error"] = error_text;
  fields["staging_dir"] = job.staging_dir;
  journal(job.operation + ".interrupted", fields);
}

std::optional<JobRecord> Workspace::loadJob(const std::string& job_id) const {
  if (job_id.empty()) {
    return std::nullopt;
  }
  const fs::path path = stateDir() / "jobs" / (job_id + ".job");
  std::error_code error;
  if (!fs::is_regular_file(path, error)) {
    return std::nullopt;
  }
  return JobRecord::fromText(readText(path));
}

std::optional<JobRecord> Workspace::pendingJob() const {
  return loadJob(loadSession().get("pending_job"));
}

std::vector<JobRecord> Workspace::listJobs() const {
  std::vector<JobRecord> jobs;
  const fs::path directory = stateDir() / "jobs";
  std::error_code error;
  if (!fs::is_directory(directory, error)) {
    return jobs;
  }
  for (const fs::directory_entry& entry :
       fs::directory_iterator(directory, error)) {
    if (error) {
      throw std::runtime_error("cannot scan jobs: " + error.message());
    }
    if (entry.is_regular_file() && entry.path().extension() == ".job") {
      jobs.push_back(JobRecord::fromText(readText(entry.path())));
    }
  }
  std::sort(jobs.begin(), jobs.end(),
            [](const JobRecord& left, const JobRecord& right) {
              return left.started_unix_ms < right.started_unix_ms;
            });
  return jobs;
}

RecoveryReport Workspace::recover() {
  RecoveryReport report;
  SessionState session = loadSession();
  std::string pending_id = session.get("pending_job");
  if (pending_id.empty()) {
    for (const JobRecord& job : listJobs()) {
      if (job.status == "staging" || job.status == "committing") {
        pending_id = job.job_id;
        break;
      }
    }
  }
  if (!pending_id.empty()) {
    const std::optional<JobRecord> loaded = loadJob(pending_id);
    if (loaded.has_value()) {
      report.had_pending = true;
      report.pending = *loaded;
      JobRecord job = *loaded;
      const fs::path final_path(job.final_dir);
      const fs::path staging_path(job.staging_dir);
      std::error_code error;
      if (fs::is_regular_file(final_path / "capture.json", error)) {
        const CaptureEvidence capture = CaptureEvidence::fromJson(
            Json::parse(readText(final_path / "capture.json")));
        recordCapture(capture, final_path);
        job.status = "complete";
        job.error.clear();
        updateJob(job);
        session = loadSession();
        session.set("pending_job", "");
        session.set("clean_exit", "1");
        session.set("last_action", "recover.finalize_final");
        saveSession(session);
        report.actions.push_back("indexed committed capture " +
                                 capture.capture_id);
      } else if (fs::is_regular_file(staging_path / "capture.json", error)) {
        const CaptureEvidence capture = CaptureEvidence::fromJson(
            Json::parse(readText(staging_path / "capture.json")));
        ensureDir(final_path.parent_path());
        fs::rename(staging_path, final_path, error);
        if (error) {
          throw std::runtime_error("recovery cannot commit staging capture: " +
                                   error.message());
        }
        fsyncDirectory(final_path);
        recordCapture(capture, final_path);
        job.status = "complete";
        job.error.clear();
        updateJob(job);
        session = loadSession();
        session.set("pending_job", "");
        session.set("clean_exit", "1");
        session.set("last_action", "recover.finalize_staging");
        saveSession(session);
        report.actions.push_back("finalized staged capture " +
                                 capture.capture_id);
      } else {
        job.status = "interrupted_after_crash";
        job.error =
            "process ended before a complete capture manifest was durable";
        updateJob(job);
        session.set("pending_job", job.job_id);
        session.set("clean_exit", "1");
        session.set("last_action", "recover.interrupted");
        saveSession(session);
        report.actions.push_back("preserved interrupted job " + job.job_id);
      }
    } else if (!pending_id.empty()) {
      session.set("pending_job", "");
      session.set("clean_exit", "1");
      session.set("last_action", "recover.missing_job");
      saveSession(session);
      report.actions.push_back("cleared missing job reference " + pending_id);
    }
  } else if (session.get("clean_exit") != "1") {
    session.set("clean_exit", "1");
    session.set("last_action", "recover.clean");
    saveSession(session);
    report.actions.push_back("marked previous session as re-entered");
  }
  if (report.had_pending || !report.actions.empty()) {
    Json fields = Json::object();
    fields["pending_job"] = report.pending.job_id;
    fields["had_pending"] = report.had_pending;
    Json actions = Json::array();
    for (const std::string& action : report.actions) {
      actions.push(action);
    }
    fields["actions"] = actions;
    journal("workspace.recover", fields);
  }
  return report;
}

void Workspace::journal(const std::string& event, const Json& fields) {
  const TimeStamp stamp = nowStamp();
  Json record = fields.isObject() ? fields : Json::object();
  record["event"] = event;
  record["event_id"] = randomToken(6);
  record["utc"] = stamp.utc;
  record["unix_ms"] = stamp.unix_ms;
  record["monotonic_ns"] = stamp.monotonic_ns;
  appendJournalLine(journalPath(), record.dump(0));
}

void Workspace::recordCapture(const CaptureEvidence& capture,
                              const fs::path& capture_dir) {
  mutateSession([&](SessionState& session) {
    const std::string anchor_key = "anchor." + capture.anchor + ".latest";
    session.set(anchor_key, capture.capture_id);
    session.set("anchor." + capture.anchor + ".utc",
                capture.completed_at.utc);
    session.set("anchor." + capture.anchor + ".dir",
                relativeToRoot(root_, capture_dir));
    for (const ViewEvidence& view : capture.views) {
      if (view.kind != "object" || view.object_id.empty()) {
        continue;
      }
      const std::string object_prefix =
          "object." + sanitizeName(view.object_id) + ".";
      session.set(object_prefix + capture.anchor + ".latest",
                  capture.capture_id);
      session.set(object_prefix + capture.anchor + ".utc",
                  view.rendered_at.utc);
      session.set(object_prefix + capture.anchor + ".raw_offset",
                  std::to_string(view.source_range.offset));
    }
    session.set("generation",
                std::to_string(session.number("generation", 1) + 1));
    session.set("last_capture_id", capture.capture_id);
    session.set("last_capture_utc", capture.completed_at.utc);
    session.set("last_action", "capture.indexed");
  });
  Json fields = Json::object();
  fields["capture_id"] = capture.capture_id;
  fields["anchor"] = capture.anchor;
  fields["capture_dir"] = relativeToRoot(root_, capture_dir);
  fields["scene_sha256"] = capture.scene_sha256;
  journal("capture.indexed", fields);
}

std::optional<CaptureEvidence> Workspace::loadCaptureAt(
    const fs::path& capture_dir) const {
  const fs::path manifest = capture_dir / "capture.json";
  std::error_code error;
  if (!fs::is_regular_file(manifest, error)) {
    return std::nullopt;
  }
  return CaptureEvidence::fromJson(Json::parse(readText(manifest)));
}

std::optional<CaptureEvidence> Workspace::loadCapture(
    const std::string& anchor, const std::string& capture_id) const {
  std::string id = capture_id;
  if (id.empty()) {
    id = latestCaptureId(anchor);
  }
  if (!id.empty()) {
    const fs::path directory = capturesDir() / anchor / id;
    const auto capture = loadCaptureAt(directory);
    if (capture.has_value()) {
      return capture;
    }
  }
  std::vector<fs::path> directories = listCaptureDirectories();
  std::sort(directories.begin(), directories.end(),
            [](const fs::path& left, const fs::path& right) {
              return left.filename().string() > right.filename().string();
            });
  for (const fs::path& directory : directories) {
    if (directory.parent_path().filename() != anchor) {
      continue;
    }
    const auto capture = loadCaptureAt(directory);
    if (capture.has_value()) {
      return capture;
    }
  }
  return std::nullopt;
}

std::vector<fs::path> Workspace::listCaptureDirectories() const {
  std::vector<fs::path> directories;
  std::error_code error;
  if (!fs::is_directory(capturesDir(), error)) {
    return directories;
  }
  for (const fs::directory_entry& anchor_entry :
       fs::directory_iterator(capturesDir(), error)) {
    if (error) {
      throw std::runtime_error("cannot scan captures: " + error.message());
    }
    if (!anchor_entry.is_directory() ||
        startsWith(anchor_entry.path().filename().string(), ".")) {
      continue;
    }
    for (const fs::directory_entry& capture_entry :
         fs::directory_iterator(anchor_entry.path(), error)) {
      if (error) {
        throw std::runtime_error("cannot scan capture anchor: " +
                                 error.message());
      }
      if (capture_entry.is_directory() &&
          !startsWith(capture_entry.path().filename().string(), ".") &&
          fs::is_regular_file(capture_entry.path() / "capture.json", error)) {
        directories.push_back(capture_entry.path());
      }
    }
  }
  return directories;
}

fs::path Workspace::captureDirectory(const CaptureEvidence& capture) const {
  return capturesDir() / capture.anchor / capture.capture_id;
}

std::string Workspace::latestCaptureId(const std::string& anchor,
                                       const std::string& object_id) const {
  const SessionState session = loadSession();
  if (!object_id.empty()) {
    const std::string object_key =
        "object." + sanitizeName(object_id) + "." + anchor + ".latest";
    const std::string value = session.get(object_key);
    if (!value.empty()) {
      return value;
    }
  }
  return session.get("anchor." + anchor + ".latest");
}

fs::path Workspace::lock() const {
  return stateDir() / "workspace.lock";
}

WorkspaceLock::WorkspaceLock(const Workspace& workspace) {
  fd_ = ::open(workspace.lock().c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
  if (fd_ < 0) {
    throw std::runtime_error("cannot open workspace lock: " +
                             posixErrorMessage(errno));
  }
  if (::flock(fd_, LOCK_EX) != 0) {
    const int saved = errno;
    ::close(fd_);
    fd_ = -1;
    throw std::runtime_error("cannot lock workspace: " +
                             posixErrorMessage(saved));
  }
}

WorkspaceLock::~WorkspaceLock() {
  if (fd_ >= 0) {
    ::flock(fd_, LOCK_UN);
    ::close(fd_);
  }
}

std::string newCaptureId(const std::string& anchor) {
  const TimeStamp stamp = nowStamp();
  return anchor + "-" + compactStamp(stamp) + "-" + randomToken(3);
}

CaptureSession::CaptureSession(Workspace workspace)
    : workspace_(std::move(workspace)) {}

Workspace& CaptureSession::workspace() {
  return workspace_;
}

CaptureEvidence CaptureSession::capture(
    const std::string& requested_anchor, const std::string& requested_object,
    const std::string& revision, const std::string& note,
    std::uint32_t width, std::uint32_t height,
    const std::string& requested_capture_id,
    const std::string& requested_job_id) {
  WorkspaceLock guard(workspace_);
  const RecoveryReport recovery = workspace_.recover();
  if (recovery.had_pending && requested_job_id.empty() &&
      recovery.pending.status != "complete") {
    throw std::runtime_error(
        "workspace has an unfinished job (" + recovery.pending.job_id +
        "); run `gbuffer-inspector resume` before starting a new capture");
  }
  if (!workspace_.verifyOriginals(true)) {
    throw std::runtime_error(
        "original material failed verification; refusing to capture");
  }
  workspace_.recordOriginalsVerified();
  const std::string lowered = toLower(requested_anchor);
  const std::string anchor = lowered == "a0"
                                 ? "A0"
                                 : lowered == "a1"
                                       ? "A1"
                                       : lowered == "a2" ? "A2" : "";
  if (anchor.empty()) {
    throw std::runtime_error("anchor must be A0, A1, or A2");
  }
  Scene scene = workspace_.loadScene(anchor != "A0");
  std::string object_filter;
  std::vector<std::string> captured_objects;
  if (anchor == "A1") {
    object_filter = requested_object;
    if (object_filter.empty()) {
      object_filter = workspace_.loadSession().get("selected_object");
    }
    if (scene.find(object_filter) == nullptr) {
      throw std::runtime_error(
          "A1 requires a valid --object; unknown object: " + object_filter);
    }
    captured_objects.push_back(object_filter);
  } else {
    for (const SceneObject& object : scene.objects) {
      captured_objects.push_back(object.id);
    }
  }
  const std::string capture_id =
      requested_capture_id.empty() ? newCaptureId(anchor)
                                   : requested_capture_id;
  const JobRecord job = workspace_.beginJob(
      "capture", anchor, object_filter, capture_id, revision, note, width,
      height);
  const fs::path staging(job.staging_dir);
  ensureDir(staging);
  ensureDir(staging / "objects");
  CaptureEvidence capture;
  capture.capture_id = capture_id;
  capture.anchor = anchor;
  capture.scope = anchor == "A1" ? "isolated" : "scene";
  capture.object_filter = object_filter;
  capture.revision = revision;
  capture.note = note;
  capture.started_at = {job.started_utc, job.started_unix_ms, 0};
  capture.width = width;
  capture.height = height;
  capture.scene_source = "originals/scene.json";
  capture.scene_sha256 = scene.source_sha256;
  capture.scene_fingerprint = scene.fingerprint();
  capture.original_sha256 = workspace_.loadOriginalManifest().sha256;
  capture.originals_verified_at =
      workspace_.loadSession().get("originals_verified_utc");
  if (!requested_job_id.empty() && requested_job_id != job.job_id) {
    capture.extra["resumed_from_job"] = requested_job_id;
  }
  capture.generation = workspace_.nextGeneration();

  std::vector<std::string> visible_objects;
  if (anchor == "A1") {
    visible_objects.push_back(object_filter);
  } else {
    visible_objects = captured_objects;
  }

  try {
    VulkanRenderer renderer(width, height, nullptr);
    capture.vulkan_device = renderer.device().name;
    capture.vulkan_driver = renderer.device().driver;
    std::uint64_t frame_index = 0;

    InterruptGuard::check("scene GBuffer pass");
    RenderedView scene_view =
        renderer.render(scene, visible_objects, "scene", "scene", "", 0,
                        frame_index++);
    SceneObject scene_context;
    scene_context.id = "scene";
    capture.views.push_back(
        finalizeView(scene_view, scene_context, staging, "scene.gbuffer.bin"));
    const char* abort_after = std::getenv("GBI_TEST_ABORT_AFTER_VIEW");
    const char* crash_after = std::getenv("GBI_TEST_CRASH_AFTER_VIEW");
    if (crash_after != nullptr && std::string(crash_after) == "1") {
      std::_Exit(137);
    }
    if (abort_after != nullptr && std::string(abort_after) == "1") {
      throw std::runtime_error(
          "simulated interruption after scene view (GBI_TEST_ABORT_AFTER_VIEW)");
    }

    for (const std::string& object_id : captured_objects) {
      InterruptGuard::check("object isolation pass");
      const auto object_index = scene.indexOf(object_id);
      if (!object_index.has_value()) {
        throw std::runtime_error("object disappeared from scene: " + object_id);
      }
      const SceneObject& object = scene.objects[*object_index];
      RenderedView object_view = renderer.render(
          scene, {object_id}, "object." + object_id, "object", object_id,
          static_cast<std::uint32_t>(*object_index + 1), frame_index++);
      const fs::path relative =
          fs::path("objects") / (sanitizeName(object_id) + ".gbuffer.bin");
      capture.views.push_back(finalizeView(object_view, object, staging,
                                           relative.generic_string()));
      if (abort_after != nullptr &&
          std::to_string(capture.views.size()) == abort_after) {
        throw std::runtime_error(
            "simulated interruption after view " +
            std::to_string(capture.views.size()) +
            " (GBI_TEST_ABORT_AFTER_VIEW)");
      }
      if (crash_after != nullptr &&
          std::to_string(capture.views.size()) == crash_after) {
        std::_Exit(137);
      }
    }
    InterruptGuard::check("capture commit");
    capture.completed_at = nowStamp();
    atomicWriteText(staging / "capture.json", capture.toJson());
    std::ostringstream sums;
    for (const ViewEvidence& view : capture.views) {
      sums << view.raw_sha256 << "  " << view.raw_file << '\n';
    }
    sums << sha256File(staging / "capture.json") << "  capture.json\n";
    atomicWriteText(staging / "SHA256SUMS", sums.str());
    fsyncDirectory(staging);
    workspace_.completeJob(job);
    return capture;
  } catch (const std::exception& error) {
    workspace_.failJob(job, error.what());
    throw;
  }
}

}  // namespace gbi
