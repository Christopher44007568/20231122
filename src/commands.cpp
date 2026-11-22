#include "gbi/commands.hpp"

#include "gbi/compare.hpp"
#include "gbi/json.hpp"
#include "gbi/util.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace gbi {

namespace {

void printRecovery(const RecoveryReport& report) {
  if (!report.had_pending) {
    return;
  }
  std::cout << "recovery: pending job " << report.pending.job_id << "\n";
  for (const std::string& action : report.actions) {
    std::cout << "recovery: " << action << "\n";
  }
}

std::string modeString(const fs::path& path) {
  std::ostringstream stream;
  stream << std::oct << (static_cast<unsigned>(
                            fs::status(path).permissions() &
                            fs::perms::mask));
  return stream.str();
}

}  // namespace

fs::path defaultOriginalAsset() {
  std::vector<fs::path> candidates;
  candidates.push_back(fs::path(GBI_SOURCE_DIR) / "assets" /
                       "original_scene.json");
  candidates.push_back(fs::current_path() / "assets" /
                       "original_scene.json");
  std::error_code error;
  const fs::path executable = fs::read_symlink("/proc/self/exe", error);
  if (!error) {
    const fs::path directory = executable.parent_path();
    candidates.push_back(directory / "assets" / "original_scene.json");
    candidates.push_back(directory / ".." / "share" / "gbuffer-inspector" /
                         "assets" / "original_scene.json");
  }
  candidates.push_back("/usr/local/share/gbuffer-inspector/assets/"
                       "original_scene.json");
  candidates.push_back("/usr/share/gbuffer-inspector/assets/"
                       "original_scene.json");
  for (const fs::path& candidate : candidates) {
    if (fs::is_regular_file(candidate, error)) {
      return fs::absolute(candidate);
    }
  }
  throw std::runtime_error("cannot locate assets/original_scene.json");
}

int commandInit(const CommonOptions& options, bool force) {
  Workspace workspace =
      Workspace::initialize(options.workspace, defaultOriginalAsset(), force);
  WorkspaceLock guard(workspace);
  const RecoveryReport recovery = workspace.recover();
  printRecovery(recovery);
  if (!workspace.verifyOriginals(true)) {
    throw std::runtime_error("original material verification failed");
  }
  const SessionState session = workspace.loadSession();
  std::cout << "workspace=" << workspace.root().string() << "\n";
  std::cout << "original=" << workspace.originalScenePath().string() << "\n";
  std::cout << "original_sha256=" << workspace.loadOriginalManifest().sha256
            << "\n";
  std::cout << "selected_object=" << session.get("selected_object") << "\n";
  std::cout << "anchors=A0,A1,A2\n";
  std::cout << "status=initialized\n";
  return 0;
}

int commandStatus(const CommonOptions& options) {
  Workspace workspace = Workspace::open(options.workspace);
  WorkspaceLock guard(workspace);
  const RecoveryReport recovery = workspace.recover();
  printRecovery(recovery);
  const SessionState session = workspace.loadSession();
  const Scene scene = workspace.loadScene();
  const OriginalManifest manifest = workspace.loadOriginalManifest();
  const bool original_ok = workspace.verifyOriginals(false);
  std::cout << "schema=gbi.status/1\n";
  std::cout << "workspace=" << workspace.root().string() << "\n";
  std::cout << "generation=" << session.get("generation") << "\n";
  std::cout << "selected_object=" << session.get("selected_object") << "\n";
  std::cout << "selected_anchor=" << session.get("selected_anchor", "A0")
            << "\n";
  std::cout << "clean_exit=" << session.get("clean_exit") << "\n";
  std::cout << "pending_job=" << session.get("pending_job") << "\n";
  std::cout << "last_action=" << session.get("last_action") << "\n";
  std::cout << "original_sha256=" << manifest.sha256 << "\n";
  std::cout << "original_mode=" << modeString(workspace.originalScenePath())
            << "\n";
  std::cout << "original_verified=" << (original_ok ? "yes" : "no") << "\n";
  for (const char* anchor : {"A0", "A1", "A2"}) {
    const std::string id = workspace.latestCaptureId(anchor);
    std::cout << "anchor." << anchor << ".capture=" << id << "\n";
    std::cout << "anchor." << anchor << ".utc="
              << session.get(std::string("anchor.") + anchor + ".utc") << "\n";
  }
  for (const SceneObject& object : scene.objects) {
    const std::string id =
        workspace.latestCaptureId("A1", object.id);
    std::cout << "object." << object.id << ".A1.capture=" << id << "\n";
    std::cout << "object." << object.id << ".A1.utc="
              << session.get("object." + sanitizeName(object.id) +
                             ".A1.utc")
              << "\n";
    std::cout << "object." << object.id
              << ".corrected=" << (object.corrected ? "yes" : "no") << "\n";
    std::cout << "object." << object.id
              << ".source_offset=" << object.source_range.offset << "\n";
  }
  return 0;
}

int commandAnchors(const CommonOptions& options) {
  Workspace workspace = Workspace::open(options.workspace);
  WorkspaceLock guard(workspace);
  workspace.recover();
  const SessionState session = workspace.loadSession();
  const Scene scene = workspace.loadScene();
  std::cout << "canonical anchors\n";
  std::cout << "A0  source      original full-scene GBuffer; immutable input\n";
  std::cout << "A1  isolate     one object rerun with a local overlay\n";
  std::cout << "A2  accept      full-scene recomposition with all overlays\n\n";
  std::cout << std::left << std::setw(5) << "anchor" << std::setw(28)
            << "latest_capture" << std::setw(14) << "utc" << "object\n";
  for (const char* anchor : {"A0", "A2"}) {
    std::cout << std::left << std::setw(5) << anchor
              << std::setw(28) << workspace.latestCaptureId(anchor)
              << std::setw(14)
              << session.get(std::string("anchor.") + anchor + ".utc")
              << "scene\n";
  }
  for (const SceneObject& object : scene.objects) {
    std::cout << std::left << std::setw(5) << "A1" << std::setw(28)
              << workspace.latestCaptureId("A1", object.id) << std::setw(14)
              << session.get("object." + sanitizeName(object.id) + ".A1.utc")
              << object.id << "\n";
  }
  return 0;
}

int commandCapture(const CaptureOptions& options) {
  Workspace workspace = Workspace::open(options.workspace);
  CaptureSession session(workspace);
  try {
    const CaptureEvidence capture =
        session.capture(options.anchor, options.object_id, options.revision,
                        options.note, options.width, options.height);
    WorkspaceLock guard(workspace);
    workspace.mutateSession([&](SessionState& state) {
      state.set("selected_anchor", capture.anchor);
      if (!capture.object_filter.empty()) {
        state.set("selected_object", capture.object_filter);
      }
    });
    std::cout << "capture_id=" << capture.capture_id << "\n";
    std::cout << "anchor=" << capture.anchor << "\n";
    std::cout << "scope=" << capture.scope << "\n";
    std::cout << "started_utc=" << capture.started_at.utc << "\n";
    std::cout << "completed_utc=" << capture.completed_at.utc << "\n";
    std::cout << "completed_unix_ms=" << capture.completed_at.unix_ms << "\n";
    std::cout << "scene_sha256=" << capture.scene_sha256 << "\n";
    for (const ViewEvidence& view : capture.views) {
      std::cout << "view." << view.view_id << ".kind=" << view.kind << "\n";
      std::cout << "view." << view.view_id
                << ".raw_file=" << view.raw_file << "\n";
      std::cout << "view." << view.view_id
                << ".raw_size=" << view.raw_size << "\n";
      std::cout << "view." << view.view_id
                << ".raw_sha256=" << view.raw_sha256 << "\n";
      std::cout << "view." << view.view_id
                << ".source_offset=" << view.source_range.offset << "\n";
      for (const ProbePixel& probe : view.probes) {
        if (probe.label.find("albedo") != std::string::npos) {
          std::cout << "view." << view.view_id << ".probe."
                    << probe.label << ".offset=" << probe.raw_offset
                    << " value=" << probe.value_hex << "\n";
          break;
        }
      }
    }
    std::cout << "status=complete\n";
  } catch (const std::exception& error) {
    std::cerr << "capture failed: " << error.what() << "\n";
    return InterruptGuard::requested() ? 130 : 3;
  }
  return 0;
}

namespace {

std::array<float, 3> parseAlbedo(const std::string& value) {
  std::array<float, 3> color{};
  if (!value.empty() && value[0] == '#') {
    if (value.size() != 7) {
      throw std::runtime_error("albedo must be #RRGGBB");
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const std::string pair = value.substr(1 + channel * 2, 2);
      char* end = nullptr;
      const unsigned long parsed = std::strtoul(pair.c_str(), &end, 16);
      if (end == nullptr || *end != '\0' || parsed > 255) {
        throw std::runtime_error("invalid albedo hex: " + value);
      }
      color[channel] = static_cast<float>(parsed) / 255.0F;
    }
    return color;
  }
  const std::vector<std::string> parts = split(value, ',');
  if (parts.size() != 3) {
    throw std::runtime_error("albedo must be #RRGGBB or r,g,b");
  }
  for (std::size_t channel = 0; channel < 3; ++channel) {
    double parsed = 0.0;
    if (!parseDouble(trim(parts[channel]), parsed) || parsed < 0.0 ||
        parsed > 1.0) {
      throw std::runtime_error("albedo component must be within [0,1]");
    }
    color[channel] = static_cast<float>(parsed);
  }
  return color;
}

}  // namespace

int commandCorrect(const CommonOptions& options, const std::string& object_id,
                   const std::string& albedo, const std::string& roughness,
                   const std::string& metallic, bool clear) {
  Workspace workspace = Workspace::open(options.workspace);
  WorkspaceLock guard(workspace);
  if (!workspace.verifyOriginals(false)) {
    throw std::runtime_error(
        "refusing correction because the original material changed");
  }
  const Scene scene = workspace.loadScene();
  if (scene.find(object_id) == nullptr) {
    throw std::runtime_error("unknown object: " + object_id);
  }
  if (clear) {
    const bool removed = workspace.clearOverlay(object_id);
    Json fields = Json::object();
    fields["object_id"] = object_id;
    fields["removed"] = removed;
    workspace.journal("overlay.clear", fields);
    workspace.mutateSession([&](SessionState& state) {
      state.set("selected_object", object_id);
      state.set("last_action", "overlay.clear");
    });
    std::cout << "object=" << object_id << "\n";
    std::cout << "overlay=cleared\n";
    std::cout << "original_sha256=" << workspace.loadOriginalManifest().sha256
              << "\n";
    return 0;
  }
  ObjectOverlay overlay;
  overlay.object_id = object_id;
  const auto existing = workspace.readOverlay(object_id);
  if (existing.has_value()) {
    overlay = *existing;
  }
  if (!albedo.empty()) {
    const auto color = parseAlbedo(albedo);
    overlay.albedo = {color[0], color[1], color[2]};
    overlay.has_albedo = true;
  }
  if (!roughness.empty()) {
    double value = 0.0;
    if (!parseDouble(roughness, value) || value < 0.0 || value > 1.0) {
      throw std::runtime_error("roughness must be within [0,1]");
    }
    overlay.roughness = static_cast<float>(value);
    overlay.has_roughness = true;
  }
  if (!metallic.empty()) {
    double value = 0.0;
    if (!parseDouble(metallic, value) || value < 0.0 || value > 1.0) {
      throw std::runtime_error("metallic must be within [0,1]");
    }
    overlay.metallic = static_cast<float>(value);
    overlay.has_metallic = true;
  }
  if (!overlay.has_albedo && !overlay.has_roughness &&
      !overlay.has_metallic) {
    throw std::runtime_error(
        "provide --albedo, --roughness, --metallic, or --clear");
  }
  workspace.writeOverlay(overlay);
  const fs::path path = workspace.overlayPath(object_id);
  const std::string text = readText(path);
  const std::string hash = sha256Hex(
      reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
  Json fields = Json::object();
  fields["object_id"] = object_id;
  fields["overlay"] =
      path.lexically_relative(workspace.root()).generic_string();
  fields["overlay_sha256"] = hash;
  fields["original_sha256"] = workspace.loadOriginalManifest().sha256;
  workspace.journal("overlay.write", fields);
  workspace.mutateSession([&](SessionState& state) {
    state.set("selected_object", object_id);
    state.set("last_action", "overlay.write");
  });
  std::cout << "object=" << object_id << "\n";
  std::cout << "overlay=" << path.string() << "\n";
  std::cout << "overlay_sha256=" << hash << "\n";
  std::cout << "original_sha256=" << workspace.loadOriginalManifest().sha256
            << "\n";
  return 0;
}

namespace {

std::string resolveObject(const Workspace& workspace,
                          const std::string& requested) {
  if (!requested.empty()) {
    return requested;
  }
  const std::string selected =
      workspace.loadSession().get("selected_object");
  if (selected.empty()) {
    throw std::runtime_error("no object selected; pass --object");
  }
  return selected;
}

fs::path saveComparison(const Workspace& workspace,
                        const ObjectComparison& comparison) {
  const fs::path directory =
      workspace.comparisonsDir() /
      sanitizeName(comparison.comparison_id + "-" + comparison.object_id);
  ensureDir(directory);
  const fs::path json = directory / "comparison.json";
  const fs::path markdown = directory / "acceptance.md";
  atomicWriteText(json, comparison.toJson());
  atomicWriteText(markdown, comparison.toMarkdown());
  std::ostringstream sums;
  sums << sha256File(json) << "  comparison.json\n";
  sums << sha256File(markdown) << "  acceptance.md\n";
  atomicWriteText(directory / "SHA256SUMS", sums.str());
  return directory;
}

void printComparison(const ObjectComparison& comparison,
                     const fs::path& directory) {
  std::cout << "comparison_id=" << comparison.comparison_id << "\n";
  std::cout << "object=" << comparison.object_id << "\n";
  std::cout << "verdict=" << (comparison.identical ? "IDENTICAL" : "CHANGED")
            << "\n";
  std::cout << "left_anchor=" << comparison.left_anchor << "\n";
  std::cout << "left_capture=" << comparison.left_capture_id << "\n";
  std::cout << "left_utc=" << comparison.left_captured_at.utc << "\n";
  std::cout << "right_anchor=" << comparison.right_anchor << "\n";
  std::cout << "right_capture=" << comparison.right_capture_id << "\n";
  std::cout << "right_utc=" << comparison.right_captured_at.utc << "\n";
  std::cout << "source_offset=" << comparison.source_range.offset << "\n";
  std::cout << "source_length=" << comparison.source_range.length << "\n";
  std::cout << "left_raw_sha256=" << comparison.left_raw_sha256 << "\n";
  std::cout << "right_raw_sha256=" << comparison.right_raw_sha256 << "\n";
  for (const AttachmentMetric& metric : comparison.metrics) {
    std::cout << "metric." << metric.attachment
              << ".left_offset=" << metric.left_offset << "\n";
    std::cout << "metric." << metric.attachment
              << ".right_offset=" << metric.right_offset << "\n";
    std::cout << "metric." << metric.attachment
              << ".changed_pixels=" << metric.changed_pixels << "\n";
    std::cout << "metric." << metric.attachment
              << ".mean_abs=" << std::fixed << std::setprecision(6)
              << metric.mean_absolute_difference << "\n";
  }
  std::cout << "evidence=" << directory.string() << "\n";
  std::cout << "acceptance=" << (directory / "acceptance.md").string() << "\n";
}

}  // namespace

int commandCompare(const CompareOptions& options) {
  Workspace workspace = Workspace::open(options.workspace);
  WorkspaceLock guard(workspace);
  const std::string object_id = resolveObject(workspace, options.object_id);
  const ObjectComparison comparison =
      compareObject(workspace, object_id, options.left_anchor,
                    options.right_anchor, options.left_capture,
                    options.right_capture);
  const fs::path directory = saveComparison(workspace, comparison);
  Json fields = Json::object();
  fields["comparison_id"] = comparison.comparison_id;
  fields["object_id"] = object_id;
  fields["left_capture_id"] = comparison.left_capture_id;
  fields["right_capture_id"] = comparison.right_capture_id;
  fields["directory"] =
      directory.lexically_relative(workspace.root()).generic_string();
  fields["verdict"] = comparison.identical ? "IDENTICAL" : "CHANGED";
  workspace.journal("comparison.write", fields);
  printComparison(comparison, directory);
  return 0;
}

int commandExport(const ExportOptions& options) {
  Workspace workspace = Workspace::open(options.workspace);
  WorkspaceLock guard(workspace);
  const std::string object_id = resolveObject(workspace, options.object_id);
  const ExportResult result =
      exportObjectComparison(workspace, object_id, options.left_anchor,
                             options.right_anchor, options.output,
                             options.left_capture, options.right_capture);
  Json fields = Json::object();
  fields["comparison_id"] = result.comparison.comparison_id;
  fields["object_id"] = object_id;
  fields["directory"] =
      result.directory.lexically_relative(workspace.root()).generic_string();
  workspace.journal("export.write", fields);
  printComparison(result.comparison, result.directory);
  std::cout << "export=" << result.directory.string() << "\n";
  std::cout << "manifest=" << (result.directory / "manifest.json").string()
            << "\n";
  std::cout << "sha256sums=" << (result.directory / "SHA256SUMS").string()
            << "\n";
  if (fs::is_regular_file(result.directory / "scene-evidence.md")) {
    std::cout << "scene_evidence="
              << (result.directory / "scene-evidence.md").string() << "\n";
  }
  return 0;
}

int commandResume(const CommonOptions& options) {
  Workspace workspace = Workspace::open(options.workspace);
  RecoveryReport recovery;
  {
    WorkspaceLock guard(workspace);
    recovery = workspace.recover();
    printRecovery(recovery);
  }
  std::optional<JobRecord> pending = workspace.pendingJob();
  if (!pending.has_value()) {
    std::cout << "pending_job=none\n";
    std::cout << "status=idle\n";
    return 0;
  }
  const JobRecord job = *pending;
  if (job.status == "complete") {
    std::cout << "pending_job=" << job.job_id << "\n";
    std::cout << "status=complete\n";
    return 0;
  }
  if (job.operation != "capture") {
    throw std::runtime_error("cannot resume unsupported operation: " +
                             job.operation);
  }
  std::cout << "resuming_job=" << job.job_id << "\n";
  std::cout << "capture_id=" << job.capture_id << "\n";
  std::cout << "anchor=" << job.anchor << "\n";
  std::cout << "object=" << job.object_id << "\n";
  CaptureSession session(workspace);
  try {
    const CaptureEvidence capture = session.capture(
        job.anchor, job.object_id, job.revision,
        job.note.empty() ? "resumed" : job.note, job.width, job.height,
        job.capture_id, job.job_id);
    {
      WorkspaceLock guard(workspace);
      workspace.mutateSession([&](SessionState& state) {
        state.set("selected_object",
                  capture.object_filter.empty()
                      ? state.get("selected_object")
                      : capture.object_filter);
        state.set("selected_anchor", capture.anchor);
        state.set("last_action", "resume.complete");
      });
    }
    std::cout << "capture_id=" << capture.capture_id << "\n";
    std::cout << "completed_utc=" << capture.completed_at.utc << "\n";
    std::cout << "status=resumed_complete\n";
  } catch (const std::exception& error) {
    std::cerr << "resume failed: " << error.what() << "\n";
    return InterruptGuard::requested() ? 130 : 3;
  }
  return 0;
}

std::string VerificationReport::toJson() const {
  Json root = Json::object();
  root["schema"] = schema;
  root["verified_at"] = verified_at;
  root["ok"] = ok;
  root["originals_checked"] = originals_checked;
  root["captures_checked"] = captures_checked;
  root["views_checked"] = views_checked;
  root["attachments_checked"] = attachments_checked;
  root["probes_checked"] = probes_checked;
  root["exports_checked"] = exports_checked;
  Json issue_array = Json::array();
  for (const VerificationIssue& issue : issues) {
    Json item = Json::object();
    item["path"] = issue.path;
    item["message"] = issue.message;
    issue_array.push(item);
  }
  root["issues"] = issue_array;
  Json warning_array = Json::array();
  for (const std::string& warning : warnings) {
    warning_array.push(warning);
  }
  root["warnings"] = warning_array;
  return root.dump(2);
}

std::string VerificationReport::toText() const {
  std::ostringstream out;
  out << "verified_at=" << verified_at << "\n";
  out << "status=" << (ok ? "PASS" : "FAIL") << "\n";
  out << "originals_checked=" << originals_checked << "\n";
  out << "captures_checked=" << captures_checked << "\n";
  out << "views_checked=" << views_checked << "\n";
  out << "attachments_checked=" << attachments_checked << "\n";
  out << "probes_checked=" << probes_checked << "\n";
  out << "exports_checked=" << exports_checked << "\n";
  for (const VerificationIssue& issue : issues) {
    out << "issue.path=" << issue.path << "\n";
    out << "issue.message=" << issue.message << "\n";
  }
  for (const std::string& warning : warnings) {
    out << "warning=" << warning << "\n";
  }
  return out.str();
}

namespace {

struct ChecksumEntry {
  std::string hash;
  std::string path;
};

std::vector<ChecksumEntry> parseChecksums(const fs::path& path) {
  std::vector<ChecksumEntry> entries;
  const std::string text = readText(path);
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.size() < 67 || line[64] != ' ' || line[65] != ' ') {
      continue;
    }
    ChecksumEntry entry;
    entry.hash = line.substr(0, 64);
    entry.path = line.substr(66);
    if (!entry.hash.empty() && !entry.path.empty()) {
      entries.push_back(std::move(entry));
    }
  }
  return entries;
}

const AttachmentEvidence* attachmentByName(const ViewEvidence& view,
                                           const std::string& name) {
  for (const AttachmentEvidence& attachment : view.attachments) {
    if (attachment.name == name) {
      return &attachment;
    }
  }
  return nullptr;
}

void verifyChecksumFile(const fs::path& directory,
                        const std::string& label,
                        VerificationReport& report,
                        const std::vector<std::string>& expected = {}) {
  const fs::path sums_path = directory / "SHA256SUMS";
  std::error_code error;
  if (!fs::is_regular_file(sums_path, error)) {
    report.issues.push_back({sums_path.string(), "missing SHA256SUMS"});
    return;
  }
  std::set<std::string> seen;
  for (const ChecksumEntry& entry : parseChecksums(sums_path)) {
    if (entry.path.find("..") != std::string::npos ||
        (!entry.path.empty() && entry.path[0] == '/')) {
      report.issues.push_back({sums_path.string(),
                               "unsafe checksum path: " + entry.path});
      continue;
    }
    seen.insert(entry.path);
    const fs::path file = directory / entry.path;
    if (!fs::is_regular_file(file, error)) {
      report.issues.push_back({file.string(), "checksum entry target missing"});
      continue;
    }
    if (sha256File(file) != entry.hash) {
      report.issues.push_back(
          {file.string(), "checksum mismatch for " + label});
    }
  }
  for (const std::string& path : expected) {
    if (seen.find(path) == seen.end()) {
      report.issues.push_back(
          {sums_path.string(), "checksum list is missing " + path});
    }
  }
}

}  // namespace

VerificationReport verifyWorkspace(const Workspace& workspace) {
  VerificationReport report;
  report.verified_at = nowStamp().utc;
  report.originals_checked = 1;
  if (!workspace.verifyOriginals(false)) {
    report.issues.push_back(
        {workspace.originalScenePath().string(),
         "original material hash, size, or read-only permission changed"});
  }
  OriginalManifest original;
  try {
    original = workspace.loadOriginalManifest();
  } catch (const std::exception& error) {
    report.issues.push_back(
        {workspace.originalManifestPath().string(), error.what()});
  }
  std::string original_text;
  try {
    original_text = readText(workspace.originalScenePath());
  } catch (const std::exception& error) {
    report.issues.push_back(
        {workspace.originalScenePath().string(), error.what()});
  }

  std::error_code fs_error;
  for (const fs::path& directory : workspace.listCaptureDirectories()) {
    ++report.captures_checked;
    CaptureEvidence capture;
    try {
      capture = CaptureEvidence::fromJson(
          Json::parse(readText(directory / "capture.json")));
    } catch (const std::exception& error) {
      report.issues.push_back(
          {(directory / "capture.json").string(), error.what()});
      continue;
    }
    if (!original.sha256.empty() &&
        capture.scene_sha256 != original.sha256) {
      report.issues.push_back(
          {(directory / "capture.json").string(),
           "capture scene_sha256 does not match frozen original"});
    }
    if (capture.original_sha256 != original.sha256) {
      report.issues.push_back(
          {(directory / "capture.json").string(),
           "capture original_sha256 does not match manifest"});
    }
    if (capture.anchor != "A0" && capture.anchor != "A1" &&
        capture.anchor != "A2") {
      report.issues.push_back(
          {(directory / "capture.json").string(), "unknown capture anchor"});
    }
    if (capture.started_at.utc.empty() || capture.completed_at.utc.empty() ||
        capture.completed_at.unix_ms < capture.started_at.unix_ms) {
      report.issues.push_back(
          {(directory / "capture.json").string(),
           "capture timestamps are missing or out of order"});
    }
    std::vector<std::string> expected_checksums = {"capture.json"};
    for (const ViewEvidence& view : capture.views) {
      expected_checksums.push_back(view.raw_file);
    }
    verifyChecksumFile(directory, capture.capture_id, report,
                       expected_checksums);
    for (const ViewEvidence& view : capture.views) {
      ++report.views_checked;
      try {
        const Bytes raw = readViewRaw(directory, view);
        for (const AttachmentEvidence& attachment : view.attachments) {
          ++report.attachments_checked;
          if (attachment.offset % 256 != 0) {
            report.issues.push_back(
                {view.raw_file,
                 "attachment " + attachment.name +
                     " offset is not 256-byte aligned"});
          }
          if (attachment.offset + attachment.length > raw.size()) {
            report.issues.push_back(
                {view.raw_file,
                 "attachment " + attachment.name + " exceeds raw file"});
          }
          if (attachment.row_stride <
              attachment.width * attachment.pixel_stride) {
            report.issues.push_back(
                {view.raw_file,
                 "attachment " + attachment.name + " row stride is too small"});
          }
        }
        for (const ProbePixel& probe : view.probes) {
          ++report.probes_checked;
          const AttachmentEvidence* attachment =
              attachmentByName(view, probe.attachment);
          if (attachment == nullptr) {
            report.issues.push_back(
                {view.raw_file,
                 "probe references unknown attachment " + probe.attachment});
            continue;
          }
          const std::uint64_t expected_offset =
              attachment->offset +
              static_cast<std::uint64_t>(probe.y) * attachment->row_stride +
              static_cast<std::uint64_t>(probe.x) * attachment->pixel_stride;
          if (expected_offset != probe.raw_offset) {
            report.issues.push_back(
                {view.raw_file,
                 "probe " + probe.label + " offset does not match layout"});
            continue;
          }
          if (probe.raw_offset + probe.byte_count > raw.size()) {
            report.issues.push_back(
                {view.raw_file,
                 "probe " + probe.label + " exceeds raw file"});
            continue;
          }
          if (hexEncode(raw.data() + probe.raw_offset, probe.byte_count) !=
              probe.value_hex) {
            report.issues.push_back(
                {view.raw_file,
                 "probe " + probe.label + " bytes do not match raw file"});
          }
        }
        if (view.kind == "object" && view.source_range.length > 0) {
          if (view.source_range.offset + view.source_range.length >
              original_text.size()) {
            report.issues.push_back(
                {view.raw_file, "object source range exceeds original"});
          } else {
            const std::string slice =
                original_text.substr(view.source_range.offset,
                                     view.source_range.length);
            if (slice.find("\"" + view.object_id + "\"") ==
                std::string::npos) {
              report.issues.push_back(
                  {view.raw_file,
                   "object source range no longer contains its id"});
            }
          }
        }
        if (view.corrected) {
          const fs::path overlay_path =
              view.overlay_path.empty()
                  ? fs::path()
                  : (fs::path(view.overlay_path).is_absolute()
                         ? fs::path(view.overlay_path)
                         : workspace.root() / view.overlay_path);
          if (overlay_path.empty() ||
              !fs::is_regular_file(overlay_path, fs_error)) {
            report.warnings.push_back(
                "overlay referenced by " + view.view_id +
                " is not present at its original path");
          } else if (sha256File(overlay_path) != view.overlay_sha256) {
            report.issues.push_back(
                {overlay_path.string(), "overlay hash changed"});
          }
        }
      } catch (const std::exception& error) {
        report.issues.push_back({view.raw_file, error.what()});
      }
    }
  }

  if (fs::is_directory(workspace.exportsDir(), fs_error)) {
    for (const fs::directory_entry& entry :
         fs::directory_iterator(workspace.exportsDir(), fs_error)) {
      if (!entry.is_directory() ||
          startsWith(entry.path().filename().string(), ".")) {
        continue;
      }
      if (fs::is_regular_file(entry.path() / "SHA256SUMS", fs_error)) {
        ++report.exports_checked;
        verifyChecksumFile(entry.path(), "export " + entry.path().filename().string(),
                           report);
      }
    }
  }
  const auto pending = workspace.pendingJob();
  if (pending.has_value() && pending->status != "complete") {
    report.warnings.push_back("unfinished job is preserved for resume: " +
                              pending->job_id);
  }
  report.ok = report.issues.empty();
  return report;
}

int commandVerify(const CommonOptions& options) {
  Workspace workspace = Workspace::open(options.workspace);
  WorkspaceLock guard(workspace);
  workspace.recover();
  const VerificationReport report = verifyWorkspace(workspace);
  const fs::path report_path = workspace.stateDir() / "verification.json";
  atomicWriteText(report_path, report.toJson());
  workspace.journal("workspace.verify",
                    [&]() {
                      Json fields = Json::object();
                      fields["ok"] = report.ok;
                      fields["issues"] = report.issues.size();
                      fields["warnings"] = report.warnings.size();
                      fields["report"] =
                          report_path.lexically_relative(workspace.root())
                              .generic_string();
                      return fields;
                    }());
  std::cout << report.toText();
  std::cout << "report=" << report_path.string() << "\n";
  return report.ok ? 0 : 4;
}

int commandDemo(const CommonOptions& options, bool force) {
  Workspace workspace =
      Workspace::initialize(options.workspace, defaultOriginalAsset(), force);
  {
    WorkspaceLock guard(workspace);
    workspace.recover();
  }
  Scene scene = workspace.loadScene();
  const std::array<std::array<float, 3>, 3> demo_colors = {
      std::array<float, 3>{0.24F, 0.55F, 0.92F},
      std::array<float, 3>{0.92F, 0.55F, 0.18F},
      std::array<float, 3>{0.62F, 0.28F, 0.88F}};
  CaptureSession session(workspace);
  const CaptureEvidence source =
      session.capture("A0", "", "demo-source", "unmodified original scene", 640,
                      360);
  std::cout << "demo.A0=" << source.capture_id << "\n";
  std::vector<fs::path> exports;
  for (std::size_t index = 0; index < scene.objects.size(); ++index) {
    const SceneObject& object = scene.objects[index];
    ObjectOverlay overlay;
    overlay.object_id = object.id;
    const auto& color = demo_colors[index % demo_colors.size()];
    overlay.albedo = {color[0], color[1], color[2]};
    overlay.roughness = 0.32F;
    overlay.has_albedo = true;
    overlay.has_roughness = true;
    {
      WorkspaceLock guard(workspace);
      workspace.writeOverlay(overlay);
      Json fields = Json::object();
      fields["object_id"] = object.id;
      fields["demo"] = true;
      fields["overlay"] = workspace.overlayPath(object.id)
                              .lexically_relative(workspace.root())
                              .generic_string();
      workspace.journal("overlay.write", fields);
    }
    const CaptureEvidence isolate = session.capture(
        "A1", object.id, "demo-isolate",
        "isolated rerun after local correction for " + object.id, 640, 360);
    const ObjectComparison comparison =
        compareObject(workspace, object.id, "A0", "A1");
    const fs::path comparison_dir = saveComparison(workspace, comparison);
    const ExportResult exported = exportObjectComparison(
        workspace, object.id, "A0", "A1");
    exports.push_back(exported.directory);
    std::cout << "demo.A1." << object.id << "=" << isolate.capture_id << "\n";
    std::cout << "demo.compare." << object.id << "="
              << comparison_dir.string() << "\n";
    std::cout << "demo.export." << object.id << "="
              << exported.directory.string() << "\n";
  }
  const CaptureEvidence accepted = session.capture(
      "A2", "", "demo-accept",
      "full-scene recomposition with all local corrections", 640, 360);
  std::cout << "demo.A2=" << accepted.capture_id << "\n";
  for (const SceneObject& object : scene.objects) {
    const ObjectComparison comparison =
        compareObject(workspace, object.id, "A0", "A2");
    const fs::path comparison_dir = saveComparison(workspace, comparison);
    const ExportResult exported = exportObjectComparison(
        workspace, object.id, "A0", "A2");
    exports.push_back(exported.directory);
    std::cout << "demo.accept." << object.id
              << ".changed_pixels="
              << [&]() {
                   for (const AttachmentMetric& metric : comparison.metrics) {
                     if (metric.attachment == "albedo") {
                       return metric.changed_pixels;
                     }
                   }
                   return std::uint64_t{0};
                 }()
              << "\n";
    std::cout << "demo.compare." << object.id << ".A2="
              << comparison_dir.string() << "\n";
    std::cout << "demo.export." << object.id << ".A2="
              << exported.directory.string() << "\n";
  }
  {
    WorkspaceLock guard(workspace);
    workspace.mutateSession([&](SessionState& state) {
      state.set("last_action", "demo.complete");
      state.set("selected_anchor", "A2");
    });
  }
  std::cout << "demo.workspace=" << workspace.root().string() << "\n";
  std::cout << "demo.export_count=" << exports.size() << "\n";
  std::cout << "status=complete\n";
  return 0;
}

}  // namespace gbi
