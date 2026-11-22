#include "report.hpp"

#include <iomanip>
#include <sstream>

namespace kfc {

namespace {

std::string text(const Json& value, const std::string& fallback = "-") {
  return value.isString() ? value.asString() : fallback;
}

std::string number(const Json& value, int precision = 4) {
  if (!value.isNumber()) return "-";
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value.asDouble();
  return out.str();
}

std::string count(const Json& value) {
  return value.isNumber() ? std::to_string(value.asInt()) : "-";
}

std::string yesNo(bool value) { return value ? "是" : "否"; }

std::string tableRow(const std::vector<std::string>& cells) {
  std::ostringstream out;
  out << "|";
  for (const std::string& cell : cells) out << " " << cell << " |";
  out << "\n";
  return out.str();
}

}  // namespace

std::string makeReviewMarkdown(const Json& record) {
  std::ostringstream out;
  const Json& input = record.at("input");
  const Json& timeline = record.at("timeline");
  const Json& missing = record.at("missing_segment");
  const Json& result = record.at("result");
  const Json& baseline = record.at("baseline_comparison");
  const Json& degraded = record.at("degraded_comparison");
  const Json& checkpoint = record.at("checkpoint_evidence");
  const Json& resource = record.at("resource_plan");
  const Json& backend = record.at("backend");
  const std::string status = text(record.at("status"));

  out << "# 骨骼动画关键帧压缩复核记录\n\n";
  out << "**状态：`" << status << "`**\n\n";
  out << "本记录由 `skeleton-kf` 自动生成。它覆盖两台设备的时钟校准、"
         "缺失片段边界、暂停/恢复状态、资源降级、前后结果比较以及可独立"
         "校验的哈希证据。\n\n";

  out << "## 结论\n\n";
  if (status == "COMPLETE") {
    out << "- 压缩结果完整：" << yesNo(result.at("complete").asBool()) << "\n";
    out << "- 关键帧数：" << count(result.at("keyframe_count")) << " / 原始融合帧 "
        << count(result.at("source_frame_count")) << "\n";
    out << "- 最大角度误差：" << number(result.at("metrics").at(
                                        "max_angular_error_deg"))
        << "°，最大位移误差："
        << number(result.at("metrics").at("max_translation_error_m")) << " m\n";
  } else {
    out << "- 当前为暂停状态，最终压缩结果尚待恢复后生成。\n";
  }
  if (baseline.contains("verdict")) {
    out << "- 暂停恢复 vs 无中断基线：`" << text(baseline.at("verdict"))
        << "`\n";
  } else {
    out << "- 基线比较：" << text(baseline.at("status"), "未提供") << "\n";
  }
  if (degraded.contains("verdict")) {
    out << "- 资源降级比较：`" << text(degraded.at("verdict")) << "`\n";
  }
  out << "\n";

  out << "## 场景\n\n";
  out << "- 场景：`" << text(input.at("scenario_id")) << "`\n";
  out << "- 动画：" << text(input.at("description")) << "\n";
  out << "- 帧间隔：" << number(input.at("frame_interval_us"), 0)
      << " us；总时长：" << number(input.at("duration_us"), 0) << " us\n";
  out << "- 角色骨骼顺序：";
  const Json& boneOrder = input.at("bone_order");
  for (std::size_t i = 0; i < boneOrder.size(); ++i) {
    if (i != 0) out << ", ";
    out << text(boneOrder.asArray()[i]);
  }
  out << "\n\n";

  out << "| 设备 | 样本数 | 锚点数 | 名义 offset (us) | 名义 drift (ppm) |\n";
  out << "|---|---:|---:|---:|---:|\n";
  for (const Json& device : input.at("devices").asArray()) {
    out << tableRow(
        {text(device.at("device_id")), count(device.at("sample_count")),
         count(device.at("sync_anchor_count")),
         number(device.at("nominal_offset_us"), 0),
         number(device.at("nominal_drift_ppm"), 0)});
  }
  out << "\n设备时钟没有使用同一时间基。实际融合前，系统对共享动作锚点做"
         "最小二乘仿射拟合；映射为 `reference_us = slope * device_us + "
         "intercept_us`。拟合残差和斜率保存在 `clock_calibration`。\n\n";

  const Json& clock = record.at("clock_calibration");
  out << "| 设备 | slope | 估计 drift (ppm) | RMS 残差 (us) | 最大残差 (us) |\n";
  out << "|---|---:|---:|---:|---:|\n";
  for (const char* deviceName : {"device_a", "device_b"}) {
    const Json& device = clock.at(deviceName);
    out << tableRow(
        {deviceName, number(device.at("slope"), 9),
         number(device.at("estimated_drift_ppm"), 3),
         number(device.at("rms_residual_us"), 2),
         number(device.at("max_abs_residual_us"), 2)});
  }
  out << "\n";

  out << "## 缺失片段\n\n";
  out << "- 预期区间：" << number(missing.at("expected_start_us"), 0) << " – "
      << number(missing.at("expected_end_us"), 0) << " us\n";
  out << "- 检测状态：`" << text(missing.at("status")) << "`\n";
  out << "- 融合后覆盖：两台都有 " << count(timeline.at("source_coverage").at(
                                          "both_devices"))
      << " 帧，仅 A " << count(timeline.at("source_coverage").at(
                                  "device_a_only"))
      << " 帧，仅 B " << count(timeline.at("source_coverage").at(
                                  "device_b_only"))
      << " 帧\n";
  out << "- 连续片段数：" << count(timeline.at("segment_count"))
      << "；硬缺口数：" << count(timeline.at("gap_count")) << "\n";
  out << "- 重叠区设备一致性：最大角度差 "
      << number(timeline.at("cross_device_agreement")
                    .at("max_angular_delta_deg"),
                6)
      << "°，最大位移差 "
      << number(timeline.at("cross_device_agreement")
                    .at("max_translation_delta_m"),
                6)
      << " m\n";
  for (const Json& gap : missing.at("observed_gaps").asArray()) {
    out << "- 观测缺口：" << number(gap.at("observed_from_us"), 0) << " – "
        << number(gap.at("observed_to_us"), 0) << " us（约 "
        << count(gap.at("missing_nominal_frames")) << " 个缺失帧）\n";
  }
  out << "- 压缩不会跨缺口插值，缺口两侧均强制保留端点。\n\n";

  out << "## 暂停与恢复\n\n";
  if (checkpoint.contains("path")) {
    out << "- 检查点文件：`" << text(checkpoint.at("path")) << "`\n";
    out << "- 暂停时已完成 chunk：" << count(checkpoint.at("paused_after_chunks"))
        << "\n";
    out << "- 暂停时已选关键帧前缀："
        << count(checkpoint.at("selected_prefix_count")) << "\n";
    out << "- 检查点 SHA-256：`" << text(checkpoint.at("sha256")) << "`\n";
    if (checkpoint.contains("resume_loaded")) {
      out << "- 恢复时重新载入检查点：" << yesNo(checkpoint.at("resume_loaded").asBool())
          << "\n";
    }
    if (baseline.contains("checkpoint_prefix")) {
      out << "- 暂停前缀与基线前缀一致："
          << yesNo(baseline.at("checkpoint_prefix").at("prefix_matches").asBool())
          << "\n";
    }
  } else {
    out << "- 本次运行未经过暂停，或最终记录未附检查点。\n";
  }
  if (baseline.contains("verdict")) {
    out << "- 恢复后最终结果与无中断基线的完整哈希比较：`"
        << text(baseline.at("verdict")) << "`\n";
  }
  out << "\n";

  out << "## 资源与后端\n\n";
  out << "- 预算：" << number(resource.at("budget_mb"), 2) << " MB；完整路径估计 "
      << number(resource.at("estimated_full_path_mb"), 2) << " MB\n";
  out << "- 选择模式：`" << text(resource.at("selected_mode")) << "`，估计峰值 "
      << number(resource.at("estimated_peak_mb"), 2) << " MB\n";
  if (resource.contains("budget_floor_exceeded") &&
      resource.at("budget_floor_exceeded").asBool()) {
    out << "- 预算低于审计所需软下限；流程进入保留证据的流式降级路径，"
           "并在 `degradations` 中显式标记。\n";
  }
  out << "- 角度/位移容差：" << number(resource.at("compression_tolerance_deg"), 3)
      << "° / " << number(resource.at("compression_tolerance_m"), 4) << " m；chunk="
      << count(resource.at("chunk_intervals")) << " intervals\n";
  out << "- 实际计算后端：`" << text(backend.at("selected")) << "`（请求 `"
      << text(backend.at("requested")) << "`）\n";
  if (backend.at("vulkan_probe").at("available").asBool()) {
    out << "- Vulkan 设备：" << text(backend.at("vulkan_probe").at("device_name"))
        << "\n";
  }
  const Json& degradations = resource.at("degradations");
  if (degradations.size() > 0) {
    out << "- 降级事件：\n";
    for (const Json& reason : degradations.asArray()) {
      out << "  - " << text(reason) << "\n";
    }
  }
  if (degraded.contains("verdict")) {
    out << "- 降级结果：" << text(degraded.at("verdict")) << "；关键帧 "
        << count(degraded.at("keyframe_count_degraded")) << " vs 完整 "
        << count(degraded.at("keyframe_count_full")) << "\n";
  }
  out << "- 降级保留输入哈希、缺口、检查点、最终结果和比较记录；没有丢弃审计证据。\n\n";

  out << "## 证据清单\n\n";
  out << "| ID | 类型 | 文件 | SHA-256 |\n|---|---|---|---|\n";
  for (const Json& evidence : record.at("evidence").asArray()) {
    out << tableRow({text(evidence.at("id")), text(evidence.at("kind")),
                     text(evidence.at("path")), text(evidence.at("sha256"))});
  }
  out << "\n";

  out << "## 同事复核步骤\n\n";
  out << "在产物目录执行：\n\n";
  out << "```bash\n";
  out << "skeleton-kf verify --record review-record.json\n";
  out << "skeleton-kf replay --record review-record.json\n";
  out << "skeleton-kf reload-check --file review-record.json\n";
  out << "skeleton-kf inspect --file review-record.json\n";
  out << "```\n\n";
  out << "`verify` 检查输入/结果/证据哈希、字段顺序、索引单调性和基线一致性；"
         "`replay` 从相同输入与后端重新计算并比较结果 SHA-256；"
         "`reload-check` 重新载入并导出 JSON，确认字段和顺序不变。\n\n";

  out << "## 字段顺序契约\n\n";
  out << "JSON 对象使用插入顺序存储，不使用排序 map。重新载入后再导出时，"
         "对象字段保持原顺序；`field_order` 列出顶层顺序，"
         "`field_order_preserved` 为 true。\n\n";
  out << "记录 ID：`" << text(record.at("record_id")) << "`  \n";
  out << "生成时间：`" << text(record.at("created_at_utc")) << "`\n";
  return out.str();
}

}  // namespace kfc
