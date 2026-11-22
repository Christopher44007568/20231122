#include "rtsync/artifact.hpp"

#include "rtsync/analyzer.hpp"
#include "rtsync/clock.hpp"
#include "rtsync/image.hpp"
#include "rtsync/report.hpp"
#include "rtsync/sha256.hpp"
#include "rtsync/trace.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace rtsync {
namespace {

bool ensure_directory(const fs::path& path, std::string* error) {
  std::error_code ec;
  fs::create_directories(path, ec);
  if (ec) {
    if (error) *error = "cannot create directory " + path.string() + ": " +
                        ec.message();
    return false;
  }
  return true;
}

bool prepare_output(const fs::path& output, std::string* error) {
  if (output.empty()) {
    if (error) *error = "artifact directory is empty";
    return false;
  }
  if (output == output.root_path() || output.string().size() < 2) {
    if (error) *error = "refusing to use a filesystem root as artifact directory";
    return false;
  }
  if (!ensure_directory(output, error)) return false;
  const char* known[] = {
      "analysis.json", "records.jsonl", "evidence.jsonl", "REVIEW.md",
      "manifest.json", "manifest.sha256", "reload_report.json",
      "reloaded.json", "gpu_probe.json"};
  for (const char* name : known) {
    std::error_code ec;
    fs::remove(output / name, ec);
  }
  for (const char* directory : {"input", "frames", "diffs"}) {
    std::error_code ec;
    fs::remove_all(output / directory, ec);
  }
  return ensure_directory(output / "input", error) &&
         ensure_directory(output / "frames", error) &&
         ensure_directory(output / "diffs", error);
}

bool stream_prefix_and_hash(const fs::path& path, std::uint64_t prefix_limit,
                            std::string* prefix, std::string* sha,
                            std::uint64_t* size, std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (error) *error = "cannot open " + path.string();
    return false;
  }
  Sha256 hash;
  std::vector<char> buffer(64 * 1024);
  std::uint64_t total = 0;
  prefix->clear();
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = input.gcount();
    if (count <= 0) continue;
    hash.update(buffer.data(), static_cast<std::size_t>(count));
    if (prefix->size() < prefix_limit) {
      const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(
          static_cast<std::uint64_t>(count), prefix_limit - prefix->size()));
      prefix->append(buffer.data(), take);
    }
    total += static_cast<std::uint64_t>(count);
  }
  if (!input.eof()) {
    if (error) *error = "read failed for " + path.string();
    return false;
  }
  *sha = hash.finish();
  *size = total;
  return true;
}

bool write_bytes(const fs::path& path, const std::string& bytes,
                 std::string* error) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  return write_file(path.string(), bytes, error);
}

std::string file_sha(const fs::path& path, std::string* error) {
  return sha256_file_hex(path.string(), error);
}

Json file_entry(const fs::path& root, const std::string& relative,
                const std::string& role) {
  const fs::path path = root / relative;
  std::string error;
  Json entry = Json::object();
  entry["path"] = relative;
  entry["role"] = role;
  entry["bytes"] = static_cast<std::uint64_t>(fs::file_size(path));
  entry["sha256"] = file_sha(path, &error);
  if (!error.empty()) entry["error"] = error;
  return entry;
}

std::string role_for(const std::string& path) {
  if (path == "analysis.json") return "analysis";
  if (path == "records.jsonl") return "exported_records";
  if (path == "evidence.jsonl") return "evidence_chain";
  if (path == "REVIEW.md") return "human_review";
  if (path.rfind("input/", 0) == 0) return "input";
  if (path.rfind("frames/", 0) == 0) return "frame";
  if (path.rfind("diffs/", 0) == 0) return "visual_diff";
  return "artifact";
}

void add_evidence_event(const std::string& type, const Json& payload,
                        std::string* previous, std::size_t* seq,
                        std::vector<std::string>* lines) {
  Json event = Json::object();
  event["seq"] = *seq;
  event["event"] = type;
  event["payload"] = payload;
  event["prev_sha256"] = *previous;
  const std::string material = canonical_json(event);
  event["sha256"] = sha256_hex(material);
  *previous = event["sha256"].get<std::string>();
  lines->push_back(canonical_json(event) + "\n");
  ++*seq;
}

std::string build_evidence(const Analysis& analysis,
                           const ReportContext& context,
                           Json* index) {
  std::vector<std::string> lines;
  std::string previous =
      "0000000000000000000000000000000000000000000000000000000000000000";
  std::size_t seq = 0;
  Json input = Json::object();
  input["sha256"] = analysis.input_sha256;
  input["bytes"] = analysis.input_bytes;
  input["complete_copy"] = context.input_complete;
  input["copy"] = context.input_copy;
  input["prefix_copy"] = context.input_prefix_copy;
  input["prefix_sha256"] = "";
  if (!context.input_prefix_copy.empty()) {
    const fs::path prefix_path =
        fs::path(context.artifact_dir) / context.input_prefix_copy;
    input["prefix_sha256"] = file_sha(prefix_path, nullptr);
  }
  input["status"] = analysis.status;
  input["status_detail"] = analysis.status_detail;
  input["parse_error"] = context.parse_error;
  input["observed_records"] = analysis.observed_records;
  input["limits"] = Json::object();
  input["limits"]["max_input_bytes"] = analysis.limits.max_input_bytes;
  input["limits"]["max_records"] = analysis.limits.max_records;
  input["limits"]["max_findings"] = analysis.limits.max_findings;
  add_evidence_event("input_observation", input, &previous, &seq, &lines);

  for (const ClockModel& clock : analysis.clocks) {
    Json payload = Json::object();
    payload["device_id"] = clock.device_id;
    payload["calibrated"] = clock.calibrated;
    payload["valid"] = clock.valid;
    payload["method"] = clock.method;
    payload["points"] = clock.calibration_points;
    payload["slope_ns_per_tick"] =
        static_cast<double>(clock.slope_ns_per_tick);
    payload["intercept_ns"] = static_cast<double>(clock.intercept_ns);
    payload["max_residual_ns"] =
        static_cast<double>(clock.max_residual_ns);
    payload["error"] = clock.error;
    add_evidence_event("clock_model", payload, &previous, &seq, &lines);
  }

  for (const Fence& fence : analysis.trace.fences) {
    const ClockModel* clock = nullptr;
    for (const ClockModel& value : analysis.clocks) {
      if (value.device_id == fence.device_id) clock = &value;
    }
    Json payload = Json::object();
    payload["id"] = fence.id;
    payload["device_id"] = fence.device_id;
    payload["command_buffer_id"] = fence.command_buffer_id;
    payload["state"] = fence.state;
    payload["signal_present"] = fence.signal.present;
    payload["signal_device_ticks"] = fence.signal.device_ticks;
    payload["signal_host_ns"] =
        fence.signal.present
            ? map_device_ticks(clock != nullptr ? *clock : ClockModel{},
                               fence.signal.device_ticks)
            : 0;
    payload["wait_present"] = fence.wait.present;
    payload["wait_device_ticks"] = fence.wait.device_ticks;
    payload["wait_host_ns"] =
        fence.wait.present
            ? map_device_ticks(clock != nullptr ? *clock : ClockModel{},
                               fence.wait.device_ticks)
            : 0;
    add_evidence_event("fence_observation", payload, &previous, &seq, &lines);
  }

  for (const Finding& finding : analysis.findings) {
    Json payload = Json::object();
    payload["id"] = finding.id;
    payload["type"] = finding.type;
    payload["severity"] = finding.severity;
    payload["message"] = finding.message;
    payload["draw_ids"] = finding.draw_ids;
    payload["resource_ids"] = finding.resource_ids;
    payload["resource_state"] = finding.resource_state;
    payload["fence_ids"] = finding.fence_ids;
    payload["command_buffer_ids"] = finding.command_buffer_ids;
    payload["frame_ids"] = finding.frame_ids;
    payload["raw_clock_evidence"] = finding.raw_clock_evidence;
    payload["normalized_clock_evidence"] =
        finding.normalized_clock_evidence;
    add_evidence_event("key_finding", payload, &previous, &seq, &lines);
  }

  for (const Frame& frame : analysis.trace.frames) {
    Json payload = Json::object();
    payload["id"] = frame.id;
    payload["group"] = frame.group;
    payload["role"] = frame.role;
    payload["draw_id"] = frame.draw_id;
    payload["framebuffer_id"] = frame.framebuffer_id;
    payload["image_file"] = frame.image_file;
    payload["image_sha256"] =
        frame.image_file.empty()
            ? std::string()
            : file_sha(fs::path(context.artifact_dir) / frame.image_file,
                       nullptr);
    add_evidence_event("frame_image", payload, &previous, &seq, &lines);
  }

  for (const VisualDiff& diff : analysis.diffs) {
    Json payload = Json::object();
    payload["id"] = diff.id;
    payload["group"] = diff.group;
    payload["golden_frame_id"] = diff.golden_frame_id;
    payload["observed_frame_id"] = diff.observed_frame_id;
    payload["diff_file"] = diff.diff_file;
    payload["diff_sha256"] = diff.diff_sha256;
    payload["changed_pixels"] = diff.changed_pixels;
    payload["total_pixels"] = diff.total_pixels;
    payload["max_channel_delta"] = diff.max_channel_delta;
    payload["mean_abs_error"] = diff.mean_abs_error;
    payload["bbox_present"] = diff.bbox_present;
    payload["bbox"] = Json::array(
        {diff.bbox_x0, diff.bbox_y0, diff.bbox_x1, diff.bbox_y1});
    add_evidence_event("visual_diff", payload, &previous, &seq, &lines);
  }

  for (const Correlation& correlation : analysis.correlations) {
    Json payload = Json::object();
    payload["id"] = correlation.id;
    payload["finding_id"] = correlation.finding_id;
    payload["draw_ids"] = correlation.draw_ids;
    payload["resource_ids"] = correlation.resource_ids;
    payload["resource_state"] = correlation.resource_state;
    payload["fence_ids"] = correlation.fence_ids;
    payload["frame_ids"] = correlation.frame_ids;
    payload["framebuffer_ids"] = correlation.framebuffer_ids;
    payload["diff_ids"] = correlation.diff_ids;
    add_evidence_event("correlation", payload, &previous, &seq, &lines);
  }

  Json tail = Json::object();
  tail["event_count"] = seq;
  tail["input_sha256"] = analysis.input_sha256;
  tail["status"] = analysis.status;
  add_evidence_event("chain_tail", tail, &previous, &seq, &lines);

  *index = Json::object();
  (*index)["event_count"] = seq;
  (*index)["head_sha256"] = previous;
  std::string joined;
  for (const std::string& line : lines) joined += line;
  (*index)["sha256"] = sha256_hex(joined);
  return joined;
}

bool write_artifacts(const Analysis& analysis, ReportContext* context,
                     std::string* error) {
  const fs::path root(context->artifact_dir);
  const std::string records_text =
      [&]() {
        std::string text;
        for (const Json& record : records_jsonl(analysis)) {
          text += canonical_json(record) + "\n";
        }
        return text;
      }();
  context->records_sha256 = sha256_hex(records_text);
  if (!write_bytes(root / "records.jsonl", records_text, error)) return false;

  const std::string evidence_text =
      build_evidence(analysis, *context, &context->evidence_index);
  if (!write_bytes(root / "evidence.jsonl", evidence_text, error)) {
    return false;
  }

  const Json analysis_value = analysis_json(analysis, *context);
  if (!save_json((root / "analysis.json").string(), analysis_value, error)) {
    return false;
  }
  if (!write_bytes(root / "REVIEW.md",
                   review_markdown(analysis, *context), error)) {
    return false;
  }

  std::vector<std::string> relative_paths;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec), end; it != end;
       it.increment(ec)) {
    if (ec) {
      if (error) *error = "cannot enumerate artifact: " + ec.message();
      return false;
    }
    if (!it->is_regular_file()) continue;
    const std::string relative =
        fs::relative(it->path(), root, ec).generic_string();
    if (relative == "manifest.json" || relative == "manifest.sha256") continue;
    relative_paths.push_back(relative);
  }
  std::sort(relative_paths.begin(), relative_paths.end());
  Json files = Json::array();
  for (const std::string& relative : relative_paths) {
    files.push_back(file_entry(root, relative, role_for(relative)));
  }
  const Json manifest = manifest_json(analysis, *context, files);
  if (!save_json((root / "manifest.json").string(), manifest, error)) {
    return false;
  }
  std::string manifest_error;
  const std::string manifest_sha =
      file_sha(root / "manifest.json", &manifest_error);
  if (!manifest_error.empty()) {
    if (error) *error = manifest_error;
    return false;
  }
  return write_bytes(root / "manifest.sha256",
                     manifest_sha + "  manifest.json\n", error);
}

}  // namespace

AnalyzeOutcome run_analysis(const AnalyzeRequest& request) {
  AnalyzeOutcome outcome;
  outcome.artifact_dir = request.output_dir;
  if (request.input_path.empty()) {
    outcome.status = "invalid_request";
    outcome.message = "--input is required";
    return outcome;
  }
  std::string error;
  const fs::path root(request.output_dir);
  if (!prepare_output(root, &error)) {
    outcome.status = "io_error";
    outcome.message = error;
    return outcome;
  }
  std::error_code ec;
  const fs::path input_path(request.input_path);
  if (!fs::is_regular_file(input_path, ec)) {
    outcome.status = "io_error";
    outcome.message = "input is not a regular file: " + request.input_path;
    return outcome;
  }
  const std::uint64_t input_size =
      static_cast<std::uint64_t>(fs::file_size(input_path, ec));
  if (ec) {
    outcome.status = "io_error";
    outcome.message = "cannot stat input: " + ec.message();
    return outcome;
  }

  ReportContext context;
  context.artifact_dir = root.string();
  Analysis analysis;
  analysis.limits = request.limits;
  analysis.input_bytes = input_size;

  if (input_size > request.limits.max_input_bytes) {
    std::string prefix;
    std::uint64_t streamed_size = 0;
    if (!stream_prefix_and_hash(input_path, kEvidencePrefixBytes, &prefix,
                                &analysis.input_sha256, &streamed_size,
                                &error)) {
      outcome.status = "io_error";
      outcome.message = error;
      return outcome;
    }
    analysis.input_bytes = streamed_size;
    context.input_complete = false;
    context.input_prefix_copy = "input/trace.prefix.bin";
    if (!write_bytes(root / context.input_prefix_copy, prefix, &error)) {
      outcome.status = "io_error";
      outcome.message = error;
      return outcome;
    }
    analysis.status = "rejected_oversized";
    std::ostringstream detail;
    detail << "input has " << input_size << " bytes, exceeding limit "
           << request.limits.max_input_bytes
           << "; parser was not run and a 64 KiB prefix plus full SHA-256 "
              "were retained";
    analysis.status_detail = detail.str();
    if (!write_artifacts(analysis, &context, &error)) {
      outcome.status = "io_error";
      outcome.message = error;
      return outcome;
    }
    outcome.ok = true;
    outcome.status = analysis.status;
    outcome.message = analysis.status_detail;
    return outcome;
  }

  std::string bytes;
  if (!read_file(input_path.string(), &bytes, &error)) {
    outcome.status = "io_error";
    outcome.message = error;
    return outcome;
  }
  analysis.input_sha256 = sha256_hex(bytes);
  analysis.input_bytes = bytes.size();
  context.input_complete = true;
  context.input_copy = "input/trace.json";
  if (!write_bytes(root / context.input_copy, bytes, &error)) {
    outcome.status = "io_error";
    outcome.message = error;
    return outcome;
  }
  const fs::path probe_source =
      input_path.parent_path() / "gpu_probe.json";
  if (fs::is_regular_file(probe_source, ec)) {
    std::string probe_bytes;
    if (!read_file(probe_source.string(), &probe_bytes, &error) ||
        !write_bytes(root / "gpu_probe.json", probe_bytes, &error)) {
      outcome.status = "io_error";
      outcome.message = error;
      return outcome;
    }
  }

  const bool whitespace_only =
      std::all_of(bytes.begin(), bytes.end(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
      });
  Json root_json = Json::object();
  bool parsed = true;
  if (!bytes.empty() && !whitespace_only) {
    try {
      root_json = Json::parse(bytes, nullptr, true, true);
    } catch (const std::exception& e) {
      parsed = false;
      context.parse_error = e.what();
    }
  }

  if (bytes.empty() || whitespace_only) {
    analysis.status = "empty";
    analysis.empty = true;
    analysis.status_detail =
        "input file is empty; an explicit empty analysis was produced";
  } else if (!parsed) {
    analysis.status = "invalid_input";
    analysis.status_detail = "JSON parse failed: " + context.parse_error;
  } else if (!parse_trace(root_json, &analysis.trace, &error)) {
    analysis.status = "invalid_input";
    analysis.status_detail = error;
  } else {
    analysis.observed_records = analysis.trace.record_count;
    if (analysis.trace.record_count > request.limits.max_records) {
      analysis.status = "rejected_record_limit";
      std::ostringstream detail;
      detail << "parsed " << analysis.trace.record_count
             << " records, exceeding limit " << request.limits.max_records
             << "; no anomaly conclusions were produced";
      analysis.status_detail = detail.str();
    } else if (analysis.trace.record_count == 0) {
      analysis.status = "empty";
      analysis.empty = true;
      analysis.status_detail =
          "trace has no device/thread/command-buffer/fence/draw/resource/"
          "framebuffer/frame records";
    } else {
      AnalyzeOptions options;
      options.input_sha256 = analysis.input_sha256;
      options.input_bytes = analysis.input_bytes;
      options.source_dir = input_path.parent_path().string();
      options.diff_dir = (root / "diffs").string();
      options.frame_dir = (root / "frames").string();
      options.limits = request.limits;
      analysis = analyze_trace(std::move(analysis.trace), options);
      analysis.input_bytes = input_size;
      analysis.input_sha256 = sha256_hex(bytes);
      analysis.observed_records = analysis.trace.record_count;
    }
  }

  if (!write_artifacts(analysis, &context, &error)) {
    outcome.status = "io_error";
    outcome.message = error;
    return outcome;
  }
  outcome.ok = true;
  outcome.status = analysis.status;
  outcome.message =
      analysis.status_detail.empty() ? "artifact written" : analysis.status_detail;
  return outcome;
}

ReloadOutcome reload_artifact(const std::string& artifact_dir,
                              const std::string& output_dir) {
  ReloadOutcome outcome;
  const fs::path source = fs::path(artifact_dir) / "analysis.json";
  std::string source_bytes;
  std::string error;
  if (!read_file(source.string(), &source_bytes, &error)) {
    outcome.message = error;
    outcome.report["status"] = "fail";
    outcome.report["error"] = error;
    return outcome;
  }
  Json loaded;
  try {
    loaded = Json::parse(source_bytes, nullptr, true, true);
  } catch (const std::exception& e) {
    outcome.message = std::string("reload parse failed: ") + e.what();
    outcome.report["status"] = "fail";
    outcome.report["error"] = outcome.message;
    return outcome;
  }
  const std::string serialized = loaded.dump(2) + "\n";
  const fs::path output =
      output_dir.empty() ? fs::path(artifact_dir + ".reload")
                         : fs::path(output_dir);
  if (!ensure_directory(output, &error)) {
    outcome.message = error;
    outcome.report["status"] = "fail";
    outcome.report["error"] = error;
    return outcome;
  }
  if (!write_bytes(output / "reloaded.json", serialized, &error)) {
    outcome.message = error;
    outcome.report["status"] = "fail";
    outcome.report["error"] = error;
    return outcome;
  }
  const bool byte_identical = serialized == source_bytes;
  std::string difference;
  Json reloaded;
  try {
    reloaded = Json::parse(serialized, nullptr, true, true);
  } catch (const std::exception& e) {
    difference = std::string("reloaded JSON parse failed: ") + e.what();
  }
  const bool ordered =
      reloaded.is_object() &&
      same_ordered_shape(loaded, reloaded, &difference);
  std::size_t field_count = 0;
  const auto count_fields = [&](const auto& self, const Json& value) -> void {
    if (value.is_object()) {
      field_count += value.size();
      for (auto it = value.begin(); it != value.end(); ++it) {
        self(self, it.value());
      }
    } else if (value.is_array()) {
      for (const Json& item : value) self(self, item);
    }
  };
  count_fields(count_fields, loaded);
  outcome.report["status"] =
      byte_identical && ordered ? "pass" : "fail";
  outcome.report["source"] = source.generic_string();
  outcome.report["reloaded"] = (output / "reloaded.json").generic_string();
  outcome.report["source_sha256"] = sha256_hex(source_bytes);
  outcome.report["reloaded_sha256"] = sha256_hex(serialized);
  outcome.report["byte_identical"] = byte_identical;
  outcome.report["fields_and_order_preserved"] = ordered;
  outcome.report["field_count"] = field_count;
  outcome.report["first_difference"] = difference;
  if (!save_json((output / "reload_report.json").string(), outcome.report,
                 &error)) {
    outcome.message = error;
    return outcome;
  }
  outcome.ok = byte_identical && ordered;
  outcome.message =
      outcome.ok ? "reload preserved fields and order byte-for-byte"
                 : "reload changed fields or field order";
  return outcome;
}

VerifyOutcome verify_artifact(const std::string& artifact_dir) {
  VerifyOutcome outcome;
  const fs::path root(artifact_dir);
  Json report = Json::object();
  report["status"] = "fail";
  report["artifact"] = root.generic_string();
  Json checks = Json::object();
  Json errors = Json::array();
  const auto fail = [&](const std::string& message) {
    errors.push_back(message);
  };
  try {
    std::string error;
    const fs::path manifest_path = root / "manifest.json";
    const fs::path digest_path = root / "manifest.sha256";
    std::string manifest_bytes;
    if (!read_file(manifest_path.string(), &manifest_bytes, &error)) {
      fail(error);
      throw std::runtime_error("missing manifest");
    }
    std::string digest_text;
    if (!read_file(digest_path.string(), &digest_text, &error)) {
      fail(error);
      throw std::runtime_error("missing manifest digest");
    }
    const std::string expected_digest = digest_text.substr(0, 64);
    const std::string actual_digest = sha256_hex(manifest_bytes);
    checks["manifest_digest"] =
        expected_digest == actual_digest ? "pass" : "fail";
    if (expected_digest != actual_digest) {
      fail("manifest.sha256 does not match manifest.json");
    }
    Json manifest;
    try {
      manifest = Json::parse(manifest_bytes, nullptr, true, true);
    } catch (const std::exception& e) {
      fail(std::string("manifest parse failed: ") + e.what());
      throw std::runtime_error("bad manifest");
    }

    std::set<std::string> expected_files;
    bool files_ok = true;
    if (!manifest.contains("files") || !manifest["files"].is_array()) {
      fail("manifest.files is missing or not an array");
      files_ok = false;
    } else {
      for (const Json& entry : manifest["files"]) {
        const std::string relative =
            entry.value("path", std::string());
        if (relative.empty() || fs::path(relative).is_absolute() ||
            relative.find("..") != std::string::npos) {
          fail("unsafe or empty manifest path: " + relative);
          files_ok = false;
          continue;
        }
        expected_files.insert(relative);
        const fs::path path = root / relative;
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) {
          fail("missing file: " + relative);
          files_ok = false;
          continue;
        }
        const std::uint64_t size =
            static_cast<std::uint64_t>(fs::file_size(path, ec));
        if (ec ||
            size != entry.value("bytes", std::uint64_t(0))) {
          fail("size mismatch: " + relative);
          files_ok = false;
        }
        const std::string sha = file_sha(path, &error);
        if (!error.empty() || sha != entry.value("sha256", std::string())) {
          fail("SHA-256 mismatch: " + relative);
          files_ok = false;
        }
      }
    }
    std::set<std::string> actual_files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end;
         it.increment(ec)) {
      if (ec) {
        fail("cannot enumerate artifact: " + ec.message());
        files_ok = false;
        break;
      }
      if (!it->is_regular_file()) continue;
      const std::string relative =
          fs::relative(it->path(), root, ec).generic_string();
      if (relative == "manifest.json" || relative == "manifest.sha256") {
        continue;
      }
      actual_files.insert(relative);
    }
    if (actual_files != expected_files) {
      fail("manifest file set differs from artifact contents");
      files_ok = false;
    }
    checks["file_hashes"] = files_ok ? "pass" : "fail";

    const fs::path analysis_path = root / "analysis.json";
    std::string analysis_bytes;
    if (!read_file(analysis_path.string(), &analysis_bytes, &error)) {
      fail(error);
      throw std::runtime_error("missing analysis");
    }
    Json analysis;
    try {
      analysis = Json::parse(analysis_bytes, nullptr, true, true);
    } catch (const std::exception& e) {
      fail(std::string("analysis parse failed: ") + e.what());
      throw std::runtime_error("bad analysis");
    }
    const std::string canonical_analysis = analysis.dump(2) + "\n";
    const bool analysis_roundtrip = canonical_analysis == analysis_bytes;
    checks["analysis_roundtrip"] =
        analysis_roundtrip ? "pass" : "fail";
    if (!analysis_roundtrip) {
      fail("analysis.json is not in canonical ordered form");
    }
    const std::string input_sha =
        analysis.value("input", Json::object())
            .value("sha256", std::string());
    if (input_sha != manifest.value("input_sha256", std::string())) {
      fail("analysis and manifest input hashes differ");
    }
    const Json input = analysis.value("input", Json::object());
    bool input_ok = true;
    if (input.value("complete_copy", false)) {
      const std::string copy = input.value("copy", std::string());
      const std::string copy_sha = file_sha(root / copy, &error);
      if (!error.empty() || copy_sha != input_sha) {
        fail("retained input copy SHA-256 does not match analysis");
        input_ok = false;
      }
    } else {
      const std::string prefix = input.value("prefix_copy", std::string());
      std::error_code prefix_ec;
      if (prefix.empty() || !fs::is_regular_file(root / prefix, prefix_ec)) {
        fail("oversized input prefix evidence is missing");
        input_ok = false;
      }
    }
    checks["input_hash"] = input_ok ? "pass" : "fail";

    std::string records_bytes;
    const fs::path records_path = root / "records.jsonl";
    bool records_ok = true;
    if (!read_file(records_path.string(), &records_bytes, &error)) {
      fail(error);
      records_ok = false;
    } else {
      std::istringstream stream(records_bytes);
      std::string line;
      std::size_t line_number = 0;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        ++line_number;
        Json record;
        try {
          record = Json::parse(line, nullptr, true, true);
        } catch (const std::exception& e) {
          fail("records line " + std::to_string(line_number) +
               " parse failed: " + e.what());
          records_ok = false;
          continue;
        }
        const std::string stored =
            record.value("record_sha256", std::string());
        record.erase("record_sha256");
        if (stored != sha256_hex(canonical_json(record))) {
          fail("records line " + std::to_string(line_number) +
               " SHA-256 mismatch");
          records_ok = false;
        }
      }
      records_ok = records_ok && line_number > 0;
      if (line_number == 0) fail("records.jsonl is empty");
    }
    const std::string records_sha = sha256_hex(records_bytes);
    if (records_sha !=
        analysis.value("exported_records", Json::object())
            .value("sha256", std::string())) {
      fail("records.jsonl hash does not match analysis");
      records_ok = false;
    }
    checks["record_hashes"] = records_ok ? "pass" : "fail";

    std::string evidence_bytes;
    bool evidence_ok = true;
    std::string chain_previous =
        "0000000000000000000000000000000000000000000000000000000000000000";
    std::size_t evidence_count = 0;
    Json input_evidence = Json::object();
    if (!read_file((root / "evidence.jsonl").string(), &evidence_bytes,
                   &error)) {
      fail(error);
      evidence_ok = false;
    } else {
      std::istringstream stream(evidence_bytes);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        Json event;
        try {
          event = Json::parse(line, nullptr, true, true);
        } catch (const std::exception& e) {
          fail("evidence line " + std::to_string(evidence_count + 1) +
               " parse failed: " + e.what());
          evidence_ok = false;
          continue;
        }
        if (event.value("seq", std::size_t(9999999)) != evidence_count) {
          fail("evidence sequence discontinuity at event " +
               std::to_string(evidence_count));
          evidence_ok = false;
        }
        if (event.value("prev_sha256", std::string()) != chain_previous) {
          fail("evidence chain predecessor mismatch at event " +
               std::to_string(evidence_count));
          evidence_ok = false;
        }
        if (event.value("event", std::string()) == "input_observation") {
          input_evidence = event.value("payload", Json::object());
        }
        const std::string stored = event.value("sha256", std::string());
        event.erase("sha256");
        const std::string calculated = sha256_hex(canonical_json(event));
        if (stored != calculated) {
          fail("evidence SHA-256 mismatch at event " +
               std::to_string(evidence_count));
          evidence_ok = false;
        }
        chain_previous = stored;
        ++evidence_count;
      }
      if (evidence_count == 0) {
        fail("evidence.jsonl is empty");
        evidence_ok = false;
      }
    }
    const Json evidence_chain =
        analysis.value("evidence_chain", Json::object());
    if (evidence_chain.value("head_sha256", std::string()) != chain_previous ||
        evidence_chain.value("event_count", std::size_t(0)) != evidence_count) {
      fail("evidence chain head/count does not match analysis");
      evidence_ok = false;
    }
    if (manifest.value("evidence_head_sha256", std::string()) !=
        chain_previous) {
      fail("evidence chain head does not match manifest");
      evidence_ok = false;
    }
    if (input_evidence.value("sha256", std::string()) != input_sha) {
      fail("input evidence SHA-256 does not match analysis");
      evidence_ok = false;
    }
    if (!input.value("complete_copy", false)) {
      const std::string prefix_copy =
          input_evidence.value("prefix_copy", std::string());
      const std::string prefix_sha =
          file_sha(root / prefix_copy, &error);
      if (!error.empty() ||
          prefix_sha != input_evidence.value("prefix_sha256", std::string())) {
        fail("oversized prefix SHA-256 does not match evidence");
        evidence_ok = false;
      }
    }
    checks["evidence_chain"] = evidence_ok ? "pass" : "fail";

    std::map<std::string, Json> frame_by_id;
    bool frames_ok = true;
    for (const Json& frame : analysis.value("frames", Json::array())) {
      const std::string id = frame.value("id", std::string());
      frame_by_id[id] = frame;
      const std::string image_file = frame.value("image_file", std::string());
      if (image_file.empty()) continue;
      const std::string image_sha = file_sha(root / image_file, &error);
      if (!error.empty() ||
          image_sha != frame.value("image_sha256", std::string())) {
        fail("frame image SHA-256 mismatch: " + id);
        frames_ok = false;
      }
    }
    checks["frame_hashes"] = frames_ok ? "pass" : "fail";

    bool diffs_ok = true;
    std::size_t diff_index = 0;
    for (const Json& diff : analysis.value("visual_diffs", Json::array())) {
      ++diff_index;
      if (diff.value("image_error", false)) continue;
      const std::string golden_file =
          diff.value("golden_file", std::string());
      const std::string observed_file =
          diff.value("observed_file", std::string());
      Image golden;
      Image observed;
      if (!load_ppm((root / golden_file).string(), &golden, &error) ||
          !load_ppm((root / observed_file).string(), &observed, &error)) {
        fail("cannot reload frame images for diff " + std::to_string(diff_index) +
             ": " + error);
        diffs_ok = false;
        continue;
      }
      if (golden.width != observed.width ||
          golden.height != observed.height) {
        fail("frame dimensions differ for diff " + std::to_string(diff_index));
        diffs_ok = false;
        continue;
      }
      std::uint64_t changed = 0;
      std::uint64_t channel_sum = 0;
      std::uint64_t max_delta = 0;
      std::uint32_t x0 = golden.width;
      std::uint32_t y0 = golden.height;
      std::uint32_t x1 = 0;
      std::uint32_t y1 = 0;
      for (std::uint32_t y = 0; y < golden.height; ++y) {
        for (std::uint32_t x = 0; x < golden.width; ++x) {
          const std::size_t pixel =
              (static_cast<std::size_t>(y) * golden.width + x) * 3;
          std::uint32_t pixel_max = 0;
          for (int channel = 0; channel < 3; ++channel) {
            const std::uint32_t a =
                golden.rgb[pixel + static_cast<std::size_t>(channel)];
            const std::uint32_t b =
                observed.rgb[pixel + static_cast<std::size_t>(channel)];
            const std::uint32_t delta = a > b ? a - b : b - a;
            channel_sum += delta;
            pixel_max = std::max(pixel_max, delta);
          }
          if (pixel_max > 0) {
            ++changed;
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x + 1);
            y1 = std::max(y1, y + 1);
          }
          max_delta = std::max<std::uint64_t>(max_delta, pixel_max);
        }
      }
      const std::uint64_t total =
          static_cast<std::uint64_t>(golden.width) * golden.height;
      const double mean =
          total == 0 ? 0.0
                     : static_cast<double>(channel_sum) /
                           (static_cast<double>(total) * 3.0);
      if (changed != diff.value("changed_pixels", std::uint64_t(0)) ||
          total != diff.value("total_pixels", std::uint64_t(0)) ||
          max_delta != diff.value("max_channel_delta", std::uint64_t(0)) ||
          std::fabs(mean - diff.value("mean_abs_error", 0.0)) > 1e-9) {
        fail("diff statistics do not recompute: " + std::to_string(diff_index));
        diffs_ok = false;
      }
      const Json bbox = diff.value("bbox", Json::object());
      if (bbox.value("present", false) &&
          (bbox.value("x0", 0u) != x0 || bbox.value("y0", 0u) != y0 ||
           bbox.value("x1", 0u) != x1 || bbox.value("y1", 0u) != y1)) {
        fail("diff bounding box does not recompute: " +
             std::to_string(diff_index));
        diffs_ok = false;
      }
      const fs::path temp_diff = root / "diffs" / ".verify_diff.tmp.ppm";
      if (!write_ppm(temp_diff.string(), make_diff_image(golden, observed),
                     &error)) {
        fail("cannot recompute diff image: " + error);
        diffs_ok = false;
        continue;
      }
      const std::string recomputed_sha = file_sha(temp_diff, &error);
      std::error_code remove_ec;
      fs::remove(temp_diff, remove_ec);
      if (!error.empty() ||
          recomputed_sha != diff.value("diff_sha256", std::string())) {
        fail("diff image SHA-256 does not recompute: " +
             std::to_string(diff_index));
        diffs_ok = false;
      }
    }
    checks["visual_diff_recompute"] = diffs_ok ? "pass" : "fail";

    bool all_checks = true;
    for (auto it = checks.begin(); it != checks.end(); ++it) {
      if (it.value() != "pass") all_checks = false;
    }
    outcome.ok = all_checks && errors.empty();
    report["status"] = outcome.ok ? "pass" : "fail";
    report["checks"] = checks;
    report["errors"] = errors;
    report["evidence_events"] = evidence_count;
    report["evidence_head_sha256"] = chain_previous;
    Json counts = Json::object();
    counts["frames"] = frame_by_id.size();
    counts["visual_diffs"] = analysis.value("visual_diffs", Json::array()).size();
    counts["findings"] = analysis.value("findings", Json::array()).size();
    counts["correlations"] =
        analysis.value("correlations", Json::array()).size();
    report["counts"] = std::move(counts);
  } catch (const std::exception& e) {
    fail(std::string("verification aborted: ") + e.what());
    report["status"] = "fail";
    report["checks"] = checks;
    report["errors"] = errors;
  }
  outcome.report = std::move(report);
  if (outcome.message.empty()) {
    outcome.message = outcome.ok ? "artifact verification passed"
                                 : "artifact verification failed";
  }
  return outcome;
}

}  // namespace rtsync
