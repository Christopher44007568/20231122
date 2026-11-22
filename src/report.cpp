#include "rtsync/report.hpp"

#include "rtsync/clock.hpp"
#include "rtsync/sha256.hpp"

#include <filesystem>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace rtsync {
namespace {

const ClockModel* clock_for(const Analysis& analysis,
                            const std::string& device_id) {
  for (const ClockModel& clock : analysis.clocks) {
    if (clock.device_id == device_id) return &clock;
  }
  return nullptr;
}

Json string_array(const std::vector<std::string>& values) {
  Json array = Json::array();
  for (const std::string& value : values) array.push_back(value);
  return array;
}

Json fence_time_json(const FenceTime& time) {
  Json value = Json::object();
  value["present"] = time.present;
  value["host_ns"] = time.host_ns;
  value["device_ticks"] = time.device_ticks;
  return value;
}

Json clock_json(const ClockModel& clock) {
  Json value = Json::object();
  value["device_id"] = clock.device_id;
  value["calibrated"] = clock.calibrated;
  value["valid"] = clock.valid;
  value["method"] = clock.method;
  value["calibration_points"] = clock.calibration_points;
  value["slope_ns_per_tick"] = static_cast<double>(clock.slope_ns_per_tick);
  value["intercept_ns"] = static_cast<double>(clock.intercept_ns);
  value["max_residual_ns"] =
      static_cast<double>(clock.max_residual_ns);
  value["error"] = clock.error;
  return value;
}

Json normalized_interval(const ClockModel* clock, std::int64_t start_ticks,
                         std::int64_t end_ticks) {
  const ClockModel identity;
  const TimeSpan span =
      map_span(clock != nullptr ? *clock : identity, start_ticks, end_ticks);
  Json value = Json::object();
  value["start_raw_ticks"] = span.raw_start;
  value["end_raw_ticks"] = span.raw_end;
  value["start_host_ns"] = span.start_ns;
  value["end_host_ns"] = span.end_ns;
  value["duration_ns"] = span.end_ns - span.start_ns;
  return value;
}

Json device_json(const Device& device) {
  Json value = Json::object();
  value["id"] = device.id;
  value["name"] = device.name;
  value["clock_frequency_hz"] = device.clock_frequency_hz;
  Json points = Json::array();
  for (const ClockPoint& point : device.calibration) {
    Json entry = Json::object();
    entry["host_ns"] = point.host_ns;
    entry["device_ticks"] = point.device_ticks;
    points.push_back(std::move(entry));
  }
  value["calibration"] = std::move(points);
  return value;
}

Json thread_json(const RenderThread& thread) {
  Json value = Json::object();
  value["id"] = thread.id;
  value["device_id"] = thread.device_id;
  value["os_thread_id"] = thread.os_thread_id;
  Json events = Json::array();
  for (const ThreadEvent& event : thread.events) {
    Json entry = Json::object();
    entry["kind"] = event.kind;
    entry["target"] = event.target;
    entry["state"] = event.state;
    entry["host_ns"] = event.host_ns;
    entry["device_ticks"] = event.device_ticks;
    events.push_back(std::move(entry));
  }
  value["events"] = std::move(events);
  return value;
}

Json command_buffer_json(const CommandBuffer& buffer) {
  Json value = Json::object();
  value["id"] = buffer.id;
  value["device_id"] = buffer.device_id;
  value["thread_id"] = buffer.thread_id;
  value["fence_id"] = buffer.fence_id;
  value["state"] = buffer.state;
  Json operations = Json::array();
  for (const CommandOp& op : buffer.operations) {
    Json entry = Json::object();
    entry["seq"] = op.seq;
    entry["op"] = op.op;
    entry["target"] = op.target;
    entry["draw_id"] = op.draw_id;
    entry["resource_id"] = op.resource_id;
    entry["from_state"] = op.from_state;
    entry["to_state"] = op.to_state;
    entry["host_ns"] = op.host_ns;
    entry["device_ticks"] = op.device_ticks;
    operations.push_back(std::move(entry));
  }
  value["operations"] = std::move(operations);
  return value;
}

Json fence_json(const Analysis& analysis, const Fence& fence) {
  const ClockModel* clock = clock_for(analysis, fence.device_id);
  Json value = Json::object();
  value["id"] = fence.id;
  value["device_id"] = fence.device_id;
  value["command_buffer_id"] = fence.command_buffer_id;
  value["state"] = fence.state;
  value["signal"] = fence_time_json(fence.signal);
  value["wait"] = fence_time_json(fence.wait);
  Json normalized = Json::object();
  normalized["signal_host_ns"] =
      fence.signal.present
          ? map_device_ticks(clock != nullptr ? *clock : ClockModel{},
                             fence.signal.device_ticks)
          : 0;
  normalized["wait_host_ns"] =
      fence.wait.present
          ? map_device_ticks(clock != nullptr ? *clock : ClockModel{},
                             fence.wait.device_ticks)
          : 0;
  normalized["signal_present"] = fence.signal.present;
  normalized["wait_present"] = fence.wait.present;
  value["normalized"] = std::move(normalized);
  return value;
}

Json draw_json(const Analysis& analysis, const DrawCall& draw) {
  const ClockModel* clock = clock_for(analysis, draw.device_id);
  Json value = Json::object();
  value["id"] = draw.id;
  value["device_id"] = draw.device_id;
  value["thread_id"] = draw.thread_id;
  value["command_buffer_id"] = draw.command_buffer_id;
  value["framebuffer_id"] = draw.framebuffer_id;
  value["resource_state"] = draw.resource_state;
  value["resource_ids"] = string_array(draw.resource_ids);
  value["interval"] = normalized_interval(clock, draw.start_device_ticks,
                                          draw.end_device_ticks);
  return value;
}

Json resource_json(const Analysis& analysis, const Resource& resource) {
  Json value = Json::object();
  value["id"] = resource.id;
  value["device_id"] = resource.device_id;
  value["kind"] = resource.kind;
  value["initial_state"] = resource.initial_state;
  Json accesses = Json::array();
  for (const ResourceAccess& access : resource.accesses) {
    const std::string device =
        access.device_id.empty() ? resource.device_id : access.device_id;
    const ClockModel* clock = clock_for(analysis, device);
    Json entry = Json::object();
    entry["device_id"] = access.device_id;
    entry["thread_id"] = access.thread_id;
    entry["draw_id"] = access.draw_id;
    entry["mode"] = access.mode;
    entry["state"] = access.state;
    entry["interval"] = normalized_interval(
        clock, access.start_device_ticks, access.end_device_ticks);
    accesses.push_back(std::move(entry));
  }
  value["accesses"] = std::move(accesses);
  return value;
}

Json frame_json(const ReportContext& context, const Frame& frame) {
  Json value = Json::object();
  value["id"] = frame.id;
  value["group"] = frame.group;
  value["role"] = frame.role;
  value["device_id"] = frame.device_id;
  value["draw_id"] = frame.draw_id;
  value["framebuffer_id"] = frame.framebuffer_id;
  value["image_file"] = frame.image_file;
  std::string image_sha;
  if (!frame.image_file.empty() && !context.artifact_dir.empty()) {
    image_sha = sha256_file_hex(
        (fs::path(context.artifact_dir) / frame.image_file).string());
  }
  value["image_sha256"] = image_sha;
  value["capture_host_ns"] = frame.host_ns;
  value["capture_device_ticks"] = frame.device_ticks;
  return value;
}

Json diff_json(const VisualDiff& diff) {
  Json value = Json::object();
  value["id"] = diff.id;
  value["group"] = diff.group;
  value["golden_frame_id"] = diff.golden_frame_id;
  value["observed_frame_id"] = diff.observed_frame_id;
  value["golden_file"] = diff.golden_file;
  value["observed_file"] = diff.observed_file;
  value["diff_file"] = diff.diff_file;
  value["golden_sha256"] = diff.golden_sha256;
  value["observed_sha256"] = diff.observed_sha256;
  value["diff_sha256"] = diff.diff_sha256;
  value["width"] = diff.width;
  value["height"] = diff.height;
  value["changed_pixels"] = diff.changed_pixels;
  value["total_pixels"] = diff.total_pixels;
  value["max_channel_delta"] = diff.max_channel_delta;
  value["mean_abs_error"] = diff.mean_abs_error;
  Json bbox = Json::object();
  bbox["present"] = diff.bbox_present;
  bbox["x0"] = diff.bbox_x0;
  bbox["y0"] = diff.bbox_y0;
  bbox["x1"] = diff.bbox_x1;
  bbox["y1"] = diff.bbox_y1;
  value["bbox"] = std::move(bbox);
  value["image_error"] = diff.image_error;
  value["error"] = diff.error;
  return value;
}

Json finding_json(const Finding& finding) {
  Json value = Json::object();
  value["id"] = finding.id;
  value["type"] = finding.type;
  value["severity"] = finding.severity;
  value["message"] = finding.message;
  value["draw_ids"] = string_array(finding.draw_ids);
  value["resource_ids"] = string_array(finding.resource_ids);
  value["fence_ids"] = string_array(finding.fence_ids);
  value["command_buffer_ids"] = string_array(finding.command_buffer_ids);
  value["frame_ids"] = string_array(finding.frame_ids);
  value["resource_state"] = finding.resource_state;
  value["raw_clock_evidence"] = finding.raw_clock_evidence;
  value["normalized_clock_evidence"] =
      finding.normalized_clock_evidence;
  return value;
}

Json correlation_json(const Correlation& correlation) {
  Json value = Json::object();
  value["id"] = correlation.id;
  value["finding_id"] = correlation.finding_id;
  value["draw_ids"] = string_array(correlation.draw_ids);
  value["resource_ids"] = string_array(correlation.resource_ids);
  value["resource_state"] = correlation.resource_state;
  value["fence_ids"] = string_array(correlation.fence_ids);
  value["command_buffer_ids"] =
      string_array(correlation.command_buffer_ids);
  value["framebuffer_ids"] = string_array(correlation.framebuffer_ids);
  value["frame_ids"] = string_array(correlation.frame_ids);
  value["diff_ids"] = string_array(correlation.diff_ids);
  return value;
}

}  // namespace

Json analysis_json(const Analysis& analysis, const ReportContext& context) {
  Json root = Json::object();
  root["schema"] = kAnalysisSchema;
  Json analyzer = Json::object();
  analyzer["name"] = "rtsync";
  analyzer["version"] = kVersion;
  analyzer["language"] = "C++17";
  analyzer["graphics_api"] = "Vulkan";
  analyzer["window_system"] = "GLFW";
  root["analyzer"] = std::move(analyzer);

  Json input = Json::object();
  input["sha256"] = analysis.input_sha256;
  input["bytes"] = analysis.input_bytes;
  input["complete_copy"] = context.input_complete;
  input["copy"] = context.input_copy;
  input["prefix_copy"] = context.input_prefix_copy;
  input["trace_schema"] = analysis.trace.schema;
  input["parse_error"] = context.parse_error;
  Json capture = Json::object();
  capture["id"] = analysis.trace.capture.id;
  capture["scenario"] = analysis.trace.capture.scenario;
  capture["started_utc"] = analysis.trace.capture.started_utc;
  capture["description"] = analysis.trace.capture.description;
  input["capture"] = std::move(capture);
  root["input"] = std::move(input);

  Json status = Json::object();
  status["code"] = analysis.status;
  status["detail"] = analysis.status_detail;
  status["empty"] = analysis.empty;
  root["status"] = std::move(status);

  Json limits = Json::object();
  limits["max_input_bytes"] = analysis.limits.max_input_bytes;
  limits["max_records"] = analysis.limits.max_records;
  limits["max_findings"] = analysis.limits.max_findings;
  root["limits"] = std::move(limits);

  Json summary = Json::object();
  summary["devices"] = analysis.trace.devices.size();
  summary["render_threads"] = analysis.trace.render_threads.size();
  summary["command_buffers"] = analysis.trace.command_buffers.size();
  summary["fences"] = analysis.trace.fences.size();
  summary["draw_calls"] = analysis.trace.draw_calls.size();
  summary["resources"] = analysis.trace.resources.size();
  summary["framebuffers"] = analysis.trace.framebuffers.size();
  summary["frames"] = analysis.trace.frames.size();
  summary["records"] = analysis.observed_records != 0
                           ? analysis.observed_records
                           : analysis.trace.record_count;
  summary["visual_diffs"] = analysis.diffs.size();
  summary["findings"] = analysis.findings.size();
  summary["correlations"] = analysis.correlations.size();
  std::size_t critical = 0;
  std::size_t high = 0;
  for (const Finding& finding : analysis.findings) {
    if (finding.severity == "critical") ++critical;
    if (finding.severity == "high") ++high;
  }
  summary["critical_findings"] = critical;
  summary["high_findings"] = high;
  root["summary"] = std::move(summary);

  Json clocks = Json::array();
  for (const ClockModel& clock : analysis.clocks) {
    clocks.push_back(clock_json(clock));
  }
  root["clock_models"] = std::move(clocks);

  Json devices = Json::array();
  for (const Device& device : analysis.trace.devices) {
    devices.push_back(device_json(device));
  }
  root["devices"] = std::move(devices);
  Json threads = Json::array();
  for (const RenderThread& thread : analysis.trace.render_threads) {
    threads.push_back(thread_json(thread));
  }
  root["render_threads"] = std::move(threads);
  Json buffers = Json::array();
  for (const CommandBuffer& buffer : analysis.trace.command_buffers) {
    buffers.push_back(command_buffer_json(buffer));
  }
  root["command_buffers"] = std::move(buffers);
  Json fences = Json::array();
  for (const Fence& fence : analysis.trace.fences) {
    fences.push_back(fence_json(analysis, fence));
  }
  root["fences"] = std::move(fences);
  Json draws = Json::array();
  for (const DrawCall& draw : analysis.trace.draw_calls) {
    draws.push_back(draw_json(analysis, draw));
  }
  root["draw_calls"] = std::move(draws);
  Json resources = Json::array();
  for (const Resource& resource : analysis.trace.resources) {
    resources.push_back(resource_json(analysis, resource));
  }
  root["resources"] = std::move(resources);
  Json framebuffers = Json::array();
  for (const Framebuffer& framebuffer : analysis.trace.framebuffers) {
    Json value = Json::object();
    value["id"] = framebuffer.id;
    value["device_id"] = framebuffer.device_id;
    value["width"] = framebuffer.width;
    value["height"] = framebuffer.height;
    value["pixel_file"] = framebuffer.pixel_file;
    value["sha256"] = framebuffer.sha256;
    framebuffers.push_back(std::move(value));
  }
  root["framebuffers"] = std::move(framebuffers);
  Json frames = Json::array();
  for (const Frame& frame : analysis.trace.frames) {
    frames.push_back(frame_json(context, frame));
  }
  root["frames"] = std::move(frames);
  Json diffs = Json::array();
  for (const VisualDiff& diff : analysis.diffs) {
    diffs.push_back(diff_json(diff));
  }
  root["visual_diffs"] = std::move(diffs);
  Json findings = Json::array();
  for (const Finding& finding : analysis.findings) {
    findings.push_back(finding_json(finding));
  }
  root["findings"] = std::move(findings);
  Json correlations = Json::array();
  for (const Correlation& correlation : analysis.correlations) {
    correlations.push_back(correlation_json(correlation));
  }
  root["correlations"] = std::move(correlations);

  Json evidence = Json::object();
  evidence["path"] = "evidence.jsonl";
  evidence["event_count"] = context.evidence_index.value("event_count", 0);
  evidence["head_sha256"] = context.evidence_index.value("head_sha256", "");
  evidence["sha256"] = context.evidence_index.value("sha256", "");
  root["evidence_chain"] = std::move(evidence);

  Json records = Json::object();
  records["path"] = "records.jsonl";
  records["sha256"] = context.records_sha256;
  root["exported_records"] = std::move(records);

  Json verification = Json::object();
  verification["canonicalization"] =
      "ordered JSON objects; fields are emitted in the order shown";
  verification["evidence_hash_input"] =
      "compact ordered JSON of {seq,event,payload,prev_sha256}";
  verification["record_hash_input"] =
      "compact ordered JSON of each record without record_sha256";
  verification["manifest"] = "manifest.json";
  verification["manifest_digest"] = "manifest.sha256";
  verification["independent_command"] =
      "rtsync verify --artifact <this-directory>";
  root["verification"] = std::move(verification);
  root["warnings"] = string_array(analysis.trace.warnings);
  return root;
}

std::string review_markdown(const Analysis& analysis,
                            const ReportContext& context) {
  std::ostringstream out;
  out << "# 渲染线程同步复核\n\n";
  out << "- 场景: `" << analysis.trace.capture.scenario << "`\n";
  out << "- 捕获: `" << analysis.trace.capture.id << "`\n";
  out << "- 状态: `" << analysis.status << "`";
  if (!analysis.status_detail.empty()) out << " — " << analysis.status_detail;
  out << "\n- 输入 SHA-256: `" << analysis.input_sha256 << "`\n";
  out << "- 输入字节: `" << analysis.input_bytes << "`\n";
  out << "- 记录数: `" << analysis.trace.record_count << "`";
  out << " (上限 `" << analysis.limits.max_records << "`)\n";
  out << "- 关键发现: `" << analysis.findings.size() << "`";
  out << "，关联记录: `" << analysis.correlations.size() << "`";
  out << "，画面差异: `" << analysis.diffs.size() << "`\n";
  out << "- 证据链头: `"
      << context.evidence_index.value("head_sha256", std::string())
      << "`\n\n";

  out << "## 时钟归一化\n\n";
  out << "| 设备 | 标定点 | 方法 | 有效 | 斜率 ns/tick | 截距 ns | 最大残差 ns |\n";
  out << "|---|---:|---|---:|---:|---:|---:|\n";
  for (const ClockModel& clock : analysis.clocks) {
    out << "| `" << clock.device_id << "` | " << clock.calibration_points
        << " | `" << clock.method << "` | " << (clock.valid ? "是" : "否")
        << " | " << std::setprecision(12)
        << static_cast<double>(clock.slope_ns_per_tick) << " | "
        << static_cast<double>(clock.intercept_ns) << " | "
        << static_cast<double>(clock.max_residual_ns) << " |\n";
  }
  out << "\n归一化公式：`host_ns = slope_ns_per_tick * device_ticks + "
         "intercept_ns`。设备原始 tick 保留在每个访问和 fence 记录中。\n\n";

  out << "## 关键发现\n\n";
  if (analysis.findings.empty()) {
    out << "无同步异常。\n\n";
  } else {
    out << "| ID | 类型 | 级别 | draw | 资源状态 | fence | 结论 |\n";
    out << "|---|---|---|---|---|---|---|\n";
    for (const Finding& finding : analysis.findings) {
      std::string draws;
      for (const std::string& draw : finding.draw_ids) {
        if (!draws.empty()) draws += ",";
        draws += "`" + draw + "`";
      }
      std::string fences;
      for (const std::string& fence : finding.fence_ids) {
        if (!fences.empty()) fences += ",";
        fences += "`" + fence + "`";
      }
      out << "| `" << finding.id << "` | `" << finding.type << "` | "
          << finding.severity << " | " << draws << " | `"
          << finding.resource_state << "` | " << fences << " | "
          << finding.message << " |\n";
    }
    out << "\n### 原始与归一化时间证据\n\n";
    for (const Finding& finding : analysis.findings) {
      if (finding.raw_clock_evidence.empty() &&
          finding.normalized_clock_evidence.empty()) {
        continue;
      }
      out << "- `" << finding.id << "`\n";
      if (!finding.raw_clock_evidence.empty()) {
        out << "  - 原始: `" << finding.raw_clock_evidence << "`\n";
      }
      if (!finding.normalized_clock_evidence.empty()) {
        out << "  - 归一化: `" << finding.normalized_clock_evidence
            << "`\n";
      }
    }
    out << "\n";
  }

  out << "## draw / 资源 / 帧缓冲 / 画面差异对应\n\n";
  if (analysis.correlations.empty()) {
    out << "没有可关联的 draw、资源和画面证据。\n\n";
  } else {
    out << "| 关联 | 发现 | draw | 资源状态 | fence | 帧 | 帧缓冲 | 差异 | 变化像素 |\n";
    out << "|---|---|---|---|---|---|---|---|---:|\n";
    for (const Correlation& correlation : analysis.correlations) {
      auto join = [](const std::vector<std::string>& values) {
        std::string text;
        for (const std::string& value : values) {
          if (!text.empty()) text += ",";
          text += "`" + value + "`";
        }
        return text;
      };
      std::uint64_t changed = 0;
      std::uint64_t total = 0;
      std::string diff_files;
      for (const VisualDiff& diff : analysis.diffs) {
        for (const std::string& id : correlation.diff_ids) {
          if (diff.id != id) continue;
          changed += diff.changed_pixels;
          total += diff.total_pixels;
          if (!diff_files.empty()) diff_files += ",";
          diff_files += "`" + diff.diff_file + "`";
        }
      }
      out << "| `" << correlation.id << "` | `" << correlation.finding_id
          << "` | " << join(correlation.draw_ids) << " | `"
          << correlation.resource_state << "` | "
          << join(correlation.fence_ids) << " | "
          << join(correlation.frame_ids) << " | "
          << join(correlation.framebuffer_ids) << " | " << diff_files
          << " | " << changed << "/" << total << " |\n";
    }
    out << "\n";
  }

  out << "## 独立复核\n\n";
  out << "在本目录执行：\n\n```sh\nrtsync verify --artifact .\n```\n\n";
  out << "复核程序会重新计算清单内所有文件 SHA-256、证据链、导出记录哈希，"
         "并从 golden/observed 像素重新计算差异统计。\n\n";
  out << "- `analysis.json` 使用固定字段顺序；`rtsync reload` 会检查字段和顺序。\n";
  out << "- `evidence.jsonl` 每行哈希输入为 `{seq,event,payload,prev_sha256}` "
         "的紧凑有序 JSON。\n";
  out << "- `records.jsonl` 每条记录哈希输入为该记录去掉 `record_sha256` "
         "后的紧凑有序 JSON。\n";
  out << "- 完整原始输入保留在 `input/trace.json`。若输入超过限制，仅保留"
         "前缀和完整文件哈希，并在状态中标记。\n";
  return out.str();
}

Json manifest_json(const Analysis& analysis, const ReportContext& context,
                   const Json& files) {
  Json root = Json::object();
  root["schema"] = "rtsync.manifest.v1";
  root["analyzer_version"] = kVersion;
  root["status"] = analysis.status;
  root["input_sha256"] = analysis.input_sha256;
  root["input_bytes"] = analysis.input_bytes;
  root["input_complete"] = context.input_complete;
  root["records_sha256"] = context.records_sha256;
  root["evidence_head_sha256"] =
      context.evidence_index.value("head_sha256", "");
  root["files"] = files;
  Json verification = Json::object();
  verification["command"] = "rtsync verify --artifact <directory>";
  verification["manifest_digest"] = "manifest.sha256";
  verification["hash_algorithm"] = "SHA-256";
  verification["json_order"] = "ordered as emitted";
  root["verification"] = std::move(verification);
  return root;
}

Json records_jsonl(const Analysis& analysis) {
  Json lines = Json::array();
  if (analysis.correlations.empty()) {
    Json record = Json::object();
    record["record_id"] = "R-summary";
    record["kind"] = "summary";
    record["status"] = analysis.status;
    record["input_sha256"] = analysis.input_sha256;
    record["records"] = analysis.trace.record_count;
    record["findings"] = analysis.findings.size();
    record["correlations"] = 0;
    const std::string material = canonical_json(record);
    record["record_sha256"] = sha256_hex(material);
    lines.push_back(std::move(record));
    return lines;
  }
  for (const Correlation& correlation : analysis.correlations) {
    Json record = Json::object();
    record["record_id"] = "R-" + correlation.id;
    record["kind"] = "correlation";
    record["correlation_id"] = correlation.id;
    record["finding_id"] = correlation.finding_id;
    record["draw_ids"] = string_array(correlation.draw_ids);
    record["resource_ids"] = string_array(correlation.resource_ids);
    record["resource_state"] = correlation.resource_state;
    record["fence_ids"] = string_array(correlation.fence_ids);
    record["command_buffer_ids"] =
        string_array(correlation.command_buffer_ids);
    record["framebuffer_ids"] = string_array(correlation.framebuffer_ids);
    record["frame_ids"] = string_array(correlation.frame_ids);
    record["diff_ids"] = string_array(correlation.diff_ids);
    record["input_sha256"] = analysis.input_sha256;
    const std::string material = canonical_json(record);
    record["record_sha256"] = sha256_hex(material);
    lines.push_back(std::move(record));
  }
  return lines;
}

}  // namespace rtsync

