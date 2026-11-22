#include "report.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace msphere {
namespace {

std::string json_escape(const std::string& value) {
  std::ostringstream out;
  for (unsigned char character : value) {
    switch (character) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (character < 0x20U) {
          out << "\\u" << std::hex << std::setfill('0') << std::setw(4)
              << static_cast<unsigned>(character) << std::dec;
        } else {
          out << static_cast<char>(character);
        }
    }
  }
  return out.str();
}

std::string json_string(const std::string& value) {
  return "\"" + json_escape(value) + "\"";
}

bool write_text(const std::filesystem::path& path, const std::string& text,
                std::string* error) {
  std::ofstream stream(path, std::ios::binary);
  if (!stream) {
    if (error != nullptr) {
      *error = "cannot open output for writing: " + path.string();
    }
    return false;
  }
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!stream) {
    if (error != nullptr) {
      *error = "cannot write output: " + path.string();
    }
    return false;
  }
  return true;
}

std::string hex_u64(uint64_t value) {
  std::ostringstream out;
  out << "0x" << std::hex << std::setfill('0') << std::setw(16) << value;
  return out.str();
}

}  // namespace

std::string utc_now_iso8601() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds =
      std::chrono::time_point_cast<std::chrono::seconds>(now);
  const auto micros =
      std::chrono::duration_cast<std::chrono::microseconds>(now - seconds)
          .count();
  const std::time_t time = std::chrono::system_clock::to_time_t(seconds);
  std::tm broken_down{};
  gmtime_r(&time, &broken_down);
  char date[32]{};
  std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &broken_down);
  char result[64]{};
  std::snprintf(result, sizeof(result), "%s.%06lldZ", date,
                static_cast<long long>(micros));
  return result;
}

uint64_t monotonic_microseconds() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

bool write_invalid_input_report(const std::filesystem::path& path,
                                const InvalidInputEvidence& evidence,
                                std::string* error) {
  std::ostringstream out;
  out << "{\n"
      << "  \"schema\": \"msphere.energy.invalid/1\",\n"
      << "  \"case\": " << json_string(evidence.case_name) << ",\n"
      << "  \"checked_at_utc\": " << json_string(evidence.checked_at_utc)
      << ",\n"
      << "  \"elapsed_us\": " << evidence.elapsed_us << ",\n"
      << "  \"input_path\": " << json_string(evidence.input_path) << ",\n"
      << "  \"file_size\": " << evidence.file_size << ",\n"
      << "  \"max_bytes\": " << evidence.max_bytes << ",\n"
      << "  \"rejected\": true,\n"
      << "  \"error_code\": " << json_string(evidence.error_code) << ",\n"
      << "  \"field\": " << json_string(evidence.field) << ",\n"
      << "  \"detail\": " << json_string(evidence.detail) << ",\n"
      << "  \"raw_offset\": " << evidence.raw_offset << ",\n"
      << "  \"observed\": " << evidence.observed << ",\n"
      << "  \"limit\": " << evidence.limit << "\n"
      << "}\n";
  return write_text(path, out.str(), error);
}

bool write_evidence_report(const std::filesystem::path& path,
                           const EvidenceReport& report, std::string* error) {
  if (report.input == nullptr || report.render == nullptr) {
    if (error != nullptr) {
      *error = "report is missing input or render data";
    }
    return false;
  }
  const ParsedInput& input = *report.input;
  const RenderResult& render = *report.render;
  std::ostringstream out;
  out << "{\n"
      << "  \"schema\": \"msphere.energy/1\",\n"
      << "  \"title\": " << json_string(report.title) << ",\n"
      << "  \"run\": {\n"
      << "    \"started_at_utc\": " << json_string(report.run_started_utc)
      << ",\n"
      << "    \"finished_at_utc\": " << json_string(report.run_finished_utc)
      << ",\n"
      << "    \"elapsed_us\": " << report.run_elapsed_us << "\n"
      << "  },\n"
      << "  \"input\": {\n"
      << "    \"source_path\": " << json_string(report.source_path) << ",\n"
      << "    \"preserved_path\": " << json_string(report.preserved_path)
      << ",\n"
      << "    \"sha256_before\": "
      << json_string(report.source_sha256_before) << ",\n"
      << "    \"sha256_after\": "
      << json_string(report.source_sha256_after) << ",\n"
      << "    \"source_unchanged\": "
      << (report.source_unchanged ? "true" : "false") << ",\n"
      << "    \"file_size\": " << input.file_size << ",\n"
      << "    \"material_count\": " << input.material_count << ",\n"
      << "    \"reflection_width\": " << input.reflection_width << ",\n"
      << "    \"reflection_height\": " << input.reflection_height << ",\n"
      << "    \"material_offset\": " << input.material_offset << ",\n"
      << "    \"reflection_offset\": " << input.reflection_offset << ",\n"
      << "    \"energy_offset\": " << input.energy_offset << ",\n"
      << "    \"energy_integral_offset\": " << input.energy_offset << "\n"
      << "  },\n";

  out << "  \"device\": {\n"
      << "    \"cxx_standard\": \"C++17\",\n"
      << "    \"application_api_version\": \"1.1.0\",\n"
      << "    \"glfw_version\": " << json_string(render.info.glfw_version)
      << ",\n"
      << "    \"glfw_platform\": " << json_string(render.info.glfw_platform)
      << ",\n"
      << "    \"vulkan_api_version\": "
      << json_string(render.info.vulkan_api_version) << ",\n"
      << "    \"device_name\": " << json_string(render.info.device_name)
      << ",\n"
      << "    \"driver_name\": " << json_string(render.info.driver_name)
      << ",\n"
      << "    \"vendor_id\": " << render.info.vendor_id << ",\n"
      << "    \"device_id\": " << render.info.device_id << ",\n"
      << "    \"queue_family\": " << render.info.queue_family_index << ",\n"
      << "    \"color_format\": " << json_string(render.info.color_format)
      << ",\n"
      << "    \"timestamp_period_ns\": " << render.info.timestamp_period_ns
      << ",\n"
      << "    \"timestamp_supported\": "
      << (render.info.timestamp_supported ? "true" : "false") << "\n"
      << "  },\n";

  const AnomalyEvidence& anomaly = render.anomaly;
  out << "  \"anomaly\": {\n"
      << "    \"instance_index\": " << anomaly.instance_index << ",\n"
      << "    \"material_index\": " << anomaly.material_index << ",\n"
      << "    \"material_count\": " << anomaly.material_count << ",\n"
      << "    \"field\": " << json_string(anomaly.field) << ",\n"
      << "    \"source_file_offset\": " << anomaly.source_file_offset << ",\n"
      << "    \"original_value\": " << anomaly.original_value << ",\n"
      << "    \"mutated_value\": " << anomaly.mutated_value << ",\n"
      << "    \"original_bits\": " << anomaly.original_bits << ",\n"
      << "    \"mutated_bits\": " << anomaly.mutated_bits << ",\n"
      << "    \"original_bytes_hex\": "
      << json_string(anomaly.original_bytes_hex) << ",\n"
      << "    \"mutated_bytes_hex\": "
      << json_string(anomaly.mutated_bytes_hex) << "\n"
      << "  },\n";

  const FrameEvidence* anomaly_draw = nullptr;
  const InstancePlacement* target_placement = nullptr;
  for (const FrameEvidence& frame : render.frames) {
    if (frame.name == "anomaly") {
      anomaly_draw = &frame;
    }
  }
  for (const InstancePlacement& placement : render.placements) {
    if (placement.index == anomaly.instance_index) {
      target_placement = &placement;
      break;
    }
  }
  const uint64_t first_diff_x =
      render.frames.empty()
          ? 0
          : report.diff.first_differing_pixel % render.frames[0].image.width;
  const uint64_t first_diff_y =
      render.frames.empty()
          ? 0
          : report.diff.first_differing_pixel / render.frames[0].image.width;
  out << "  \"correlation\": {\n"
      << "    \"anomaly_instance\": " << anomaly.instance_index << ",\n"
      << "    \"relative_instance\": "
      << (anomaly.instance_index -
          (anomaly_draw != nullptr ? anomaly_draw->draw.first_instance : 0U))
      << ",\n"
      << "    \"draw_call_sequence\": "
      << (anomaly_draw != nullptr ? anomaly_draw->draw.sequence : 0U) << ",\n"
      << "    \"source_material_offset\": " << anomaly.source_file_offset
      << ",\n"
      << "    \"gpu_material_buffer_offset\": "
      << static_cast<uint64_t>(anomaly.instance_index) * 32U << ",\n"
      << "    \"framebuffer\": "
      << json_string(hex_u64(anomaly_draw != nullptr
                                 ? anomaly_draw->draw.framebuffer
                                 : 0U))
      << ",\n"
      << "    \"color_image\": "
      << json_string(hex_u64(anomaly_draw != nullptr
                                 ? anomaly_draw->draw.color_image
                                 : 0U))
      << ",\n"
      << "    \"first_diff_raw_rgba_offset\": "
      << report.diff.first_differing_raw_offset << ",\n"
      << "    \"first_diff_pixel\": " << report.diff.first_differing_pixel
      << ",\n"
      << "    \"first_diff_xy\": [" << first_diff_x << ", " << first_diff_y
      << "],\n"
      << "    \"diff_bounds\": [" << report.diff.bounds.min_x << ", "
      << report.diff.bounds.min_y << ", " << report.diff.bounds.max_x << ", "
      << report.diff.bounds.max_y << "],\n"
      << "    \"instance_screen_cell\": ["
      << (target_placement != nullptr ? target_placement->x0 : 0U) << ", "
      << (target_placement != nullptr ? target_placement->y0 : 0U) << ", "
      << (target_placement != nullptr ? target_placement->x1 : 0U) << ", "
      << (target_placement != nullptr ? target_placement->y1 : 0U) << "]\n"
      << "  },\n";

  out << "  \"timestamps\": [\n";
  for (size_t i = 0; i < report.phases.size(); ++i) {
    const PhaseEvent& phase = report.phases[i];
    out << "    {\"name\": " << json_string(phase.name)
        << ", \"timestamp_utc\": " << json_string(phase.timestamp_utc)
        << ", \"elapsed_us\": " << phase.elapsed_us << ", \"detail\": "
        << json_string(phase.detail) << "}";
    out << (i + 1U == report.phases.size() ? "\n" : ",\n");
  }
  out << "  ],\n";

  out << "  \"draw_calls\": [\n";
  for (size_t i = 0; i < render.frames.size(); ++i) {
    const FrameEvidence& frame = render.frames[i];
    const DrawCallEvidence& draw = frame.draw;
    out << "    {\"sequence\": " << draw.sequence << ", \"frame\": "
        << json_string(draw.frame) << ", \"first_index\": "
        << draw.first_index << ", \"index_count\": " << draw.index_count
        << ", \"vertex_offset\": " << draw.vertex_offset
        << ", \"first_instance\": " << draw.first_instance
        << ", \"instance_count\": " << draw.instance_count
        << ", \"command_buffer\": " << json_string(hex_u64(draw.command_buffer))
        << ", \"framebuffer\": " << json_string(hex_u64(draw.framebuffer))
        << ", \"color_image\": " << json_string(hex_u64(draw.color_image))
        << ", \"depth_image\": " << json_string(hex_u64(draw.depth_image))
        << ", \"material_buffer\": "
        << json_string(hex_u64(draw.material_buffer))
        << ", \"instance_buffer\": "
        << json_string(hex_u64(draw.instance_buffer))
        << ", \"material_byte_offset\": " << draw.material_byte_offset
        << ", \"instance_byte_offset\": " << draw.instance_byte_offset
        << ", \"material_content_sha256\": "
        << json_string(draw.material_content_sha256)
        << ", \"gpu_start_tick\": " << draw.gpu.start_ticks
        << ", \"gpu_end_tick\": " << draw.gpu.end_ticks
        << ", \"gpu_elapsed_ns\": " << std::fixed << std::setprecision(3)
        << draw.gpu.elapsed_ns << std::defaultfloat
        << ", \"gpu_timestamp_valid\": "
        << (draw.gpu.valid ? "true" : "false") << "}";
    out << (i + 1U == render.frames.size() ? "\n" : ",\n");
  }
  out << "  ],\n";

  out << "  \"resources\": {\n"
      << "    \"buffers\": [\n";
  for (size_t i = 0; i < render.buffers.size(); ++i) {
    const BufferEvidence& buffer = render.buffers[i];
    out << "      {\"name\": " << json_string(buffer.name)
        << ", \"handle\": " << json_string(hex_u64(buffer.handle))
        << ", \"size\": " << buffer.size << ", \"usage\": " << buffer.usage
        << ", \"content_sha256\": "
        << json_string(buffer.content_sha256) << "}";
    out << (i + 1U == render.buffers.size() ? "\n" : ",\n");
  }
  out << "    ],\n"
      << "    \"images\": [\n";
  for (size_t i = 0; i < render.images.size(); ++i) {
    const ImageEvidence& image = render.images[i];
    out << "      {\"name\": " << json_string(image.name)
        << ", \"handle\": " << json_string(hex_u64(image.handle))
        << ", \"view\": " << json_string(hex_u64(image.view))
        << ", \"width\": " << image.width << ", \"height\": " << image.height
        << ", \"format\": " << json_string(image.format)
        << ", \"usage\": " << image.usage << ", \"layout_before\": "
        << json_string(image.layout_before) << ", \"layout_after\": "
        << json_string(image.layout_after) << ", \"layout_transitions\": "
        << json_string(image.transitions) << "}";
    out << (i + 1U == render.images.size() ? "\n" : ",\n");
  }
  out << "    ]\n"
      << "  },\n";

  out << "  \"framebuffers\": [\n";
  for (size_t i = 0; i < render.frames.size(); ++i) {
    const FrameEvidence& frame = render.frames[i];
    out << "    {\"frame\": " << json_string(frame.name)
        << ", \"width\": " << frame.image.width
        << ", \"height\": " << frame.image.height
        << ", \"pixel_format\": \"RGBA8\"}";
    out << (i + 1U == render.frames.size() ? "\n" : ",\n");
  }
  out << "  ],\n";

  out << "  \"energy_comparison\": [\n";
  for (size_t i = 0; i < report.energy_samples.size(); ++i) {
    const EnergySample& sample = report.energy_samples[i];
    out << "    {\"instance_index\": " << sample.instance_index
        << ", \"expected_energy_integral\": " << sample.expected_energy
        << ", \"cell\": [" << sample.x0 << ", " << sample.y0 << ", "
        << sample.x1 << ", " << sample.y1 << "]"
        << ", \"baseline_mean_luma\": " << sample.baseline_mean_luma
        << ", \"anomaly_mean_luma\": " << sample.anomaly_mean_luma
        << ", \"relative_change\": " << sample.relative_change << "}";
    out << (i + 1U == report.energy_samples.size() ? "\n" : ",\n");
  }
  out << "  ],\n";

  const DiffBounds& bounds = report.diff.bounds;
  out << "  \"image_difference\": {\n"
      << "    \"differing_pixels\": " << report.diff.differing_pixels << ",\n"
      << "    \"differing_bytes\": " << report.diff.differing_bytes << ",\n"
      << "    \"max_channel_delta\": " << report.diff.max_channel_delta << ",\n"
      << "    \"mean_channel_delta\": " << report.diff.mean_channel_delta
      << ",\n"
      << "    \"first_differing_pixel\": " << report.diff.first_differing_pixel
      << ",\n"
      << "    \"first_differing_raw_offset\": "
      << report.diff.first_differing_raw_offset << ",\n"
      << "    \"bounds\": {\"valid\": " << (bounds.valid ? "true" : "false")
      << ", \"min_x\": " << bounds.min_x << ", \"min_y\": " << bounds.min_y
      << ", \"max_x\": " << bounds.max_x << ", \"max_y\": " << bounds.max_y
      << "}\n"
      << "  },\n";

  out << "  \"invalid_inputs\": [\n";
  for (size_t i = 0; i < report.invalid_inputs.size(); ++i) {
    const InvalidInputEvidence& invalid = report.invalid_inputs[i];
    out << "    {\"case\": " << json_string(invalid.case_name)
        << ", \"checked_at_utc\": " << json_string(invalid.checked_at_utc)
        << ", \"elapsed_us\": " << invalid.elapsed_us
        << ", \"input_path\": " << json_string(invalid.input_path)
        << ", \"file_size\": " << invalid.file_size
        << ", \"max_bytes\": " << invalid.max_bytes
        << ", \"error_code\": " << json_string(invalid.error_code)
        << ", \"field\": " << json_string(invalid.field)
        << ", \"detail\": " << json_string(invalid.detail)
        << ", \"raw_offset\": " << invalid.raw_offset
        << ", \"observed\": " << invalid.observed
        << ", \"limit\": " << invalid.limit << "}";
    out << (i + 1U == report.invalid_inputs.size() ? "\n" : ",\n");
  }
  out << "  ],\n";

  out << "  \"acceptance_checks\": [\n";
  for (size_t i = 0; i < report.checks.size(); ++i) {
    const AcceptanceCheck& check = report.checks[i];
    out << "    {\"id\": " << json_string(check.id) << ", \"passed\": "
        << (check.passed ? "true" : "false")
        << ", \"expected\": " << json_string(check.expected)
        << ", \"actual\": " << json_string(check.actual) << "}";
    out << (i + 1U == report.checks.size() ? "\n" : ",\n");
  }
  out << "  ],\n";

  out << "  \"artifacts\": [\n";
  for (size_t i = 0; i < report.artifacts.size(); ++i) {
    const ArtifactEvidence& artifact = report.artifacts[i];
    out << "    {\"role\": " << json_string(artifact.role)
        << ", \"path\": " << json_string(artifact.path)
        << ", \"size\": " << artifact.size
        << ", \"sha256\": " << json_string(artifact.sha256)
        << ", \"layout\": " << json_string(artifact.layout) << "}";
    out << (i + 1U == report.artifacts.size() ? "\n" : ",\n");
  }
  out << "  ]\n"
      << "}\n";
  return write_text(path, out.str(), error);
}

bool write_acceptance_markdown(const std::filesystem::path& path,
                               const EvidenceReport& report,
                               std::string* error) {
  if (report.input == nullptr || report.render == nullptr) {
    if (error != nullptr) {
      *error = "acceptance report is missing input or render data";
    }
    return false;
  }
  const ParsedInput& input = *report.input;
  const RenderResult& render = *report.render;
  const FrameEvidence* baseline = nullptr;
  const FrameEvidence* anomaly_frame = nullptr;
  for (const FrameEvidence& frame : render.frames) {
    if (frame.name == "baseline") {
      baseline = &frame;
    } else if (frame.name == "anomaly") {
      anomaly_frame = &frame;
    }
  }
  std::ostringstream out;
  out << "# 材质球能量对照验收\n\n"
      << "本文件由 `--self-test` 生成，所有时间点均为实际运行观测值，所有偏移均指向保留的原始字节流。\n\n"
      << "- 运行开始：`" << report.run_started_utc << "`\n"
      << "- 运行结束：`" << report.run_finished_utc << "`\n"
      << "- 总耗时：`" << report.run_elapsed_us << " us`\n"
      << "- 原始材料：`" << report.source_path << "`\n"
      << "- 未修改副本：`" << report.preserved_path << "`\n"
      << "- SHA-256（运行前）：`" << report.source_sha256_before << "`\n"
      << "- SHA-256（运行后）：`" << report.source_sha256_after << "`\n"
      << "- 原始字节未改动：`" << (report.source_unchanged ? "true" : "false")
      << "`\n\n"
      << "## 时间点\n\n"
      << "| 事件 | UTC 时间 | 相对耗时 (us) | 说明 |\n"
      << "|---|---|---:|---|\n";
  for (const PhaseEvent& phase : report.phases) {
    out << "| `" << phase.name << "` | `" << phase.timestamp_utc << "` | "
        << phase.elapsed_us << " | " << phase.detail << " |\n";
  }
  out << "\n## 原始偏移\n\n"
      << "| 字段 | 原始偏移 (byte) | 观测值 |\n"
      << "|---|---:|---|\n"
      << "| header.magic | 0 | `" << input.bytes[0] << input.bytes[1]
      << input.bytes[2] << input.bytes[3] << "...` |\n"
      << "| material_offset | " << input.material_offset << " | "
      << input.material_count << " 条 32-byte 材质记录 |\n"
      << "| reflection_offset | " << input.reflection_offset << " | "
      << input.reflection_width << "x" << input.reflection_height
      << " RGBA8 |\n"
      << "| energy_offset | " << input.energy_offset << " | "
      << input.material_count << " 个 float32 能量积分 |\n"
      << "| anomaly.field | " << render.anomaly.source_file_offset << " | `"
      << render.anomaly.field << "` 原始字节 `"
      << render.anomaly.original_bytes_hex << "` -> `"
      << render.anomaly.mutated_bytes_hex << "` |\n"
      << "| baseline.rgba first diff | " << report.diff.first_differing_raw_offset
      << " | pixel " << report.diff.first_differing_pixel << " |\n\n"
      << "异常实例 `" << render.anomaly.instance_index
      << "` 对应原始材质记录偏移 `"
      << (input.material_offset +
          static_cast<uint64_t>(render.anomaly.material_index) * kMaterialSize)
      << "`，字段偏移再增加 0 字节；注入仅发生在 GPU 上传副本，原始文件未写入。\n\n"
      << "## Draw call 与帧缓冲\n\n"
      << "| 帧 | first_instance | instance_count | index_count | GPU start tick | GPU end tick | GPU ns |\n"
      << "|---|---:|---:|---:|---:|---:|---:|\n";
  auto emit_draw = [&](const FrameEvidence* frame) {
    if (frame == nullptr) {
      out << "| missing | - | - | - | - | - | - |\n";
      return;
    }
    out << "| `" << frame->name << "` | " << frame->draw.first_instance
        << " | " << frame->draw.instance_count << " | "
        << frame->draw.index_count << " | " << frame->draw.gpu.start_ticks
        << " | " << frame->draw.gpu.end_ticks << " | " << std::fixed
        << std::setprecision(3) << frame->draw.gpu.elapsed_ns
        << std::defaultfloat << " |\n";
  };
  emit_draw(baseline);
  emit_draw(anomaly_frame);
  const InstancePlacement* target_placement = nullptr;
  for (const InstancePlacement& placement : render.placements) {
    if (placement.index == render.anomaly.instance_index) {
      target_placement = &placement;
      break;
    }
  }
  out << "\n两帧共享同一 color image/framebuffer；布局按 "
         "`UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL -> "
         "COLOR_ATTACHMENT_OPTIMAL` 转换，异常帧结束后再次回读。\n\n"
      << "关联链：异常帧 draw sequence `"
      << (anomaly_frame != nullptr ? anomaly_frame->draw.sequence : 0U)
      << "`，`first_instance=0` 中的实例 `" << render.anomaly.instance_index
      << "`；原始材质记录偏移 `" << render.anomaly.source_file_offset
      << "`，GPU 材质缓冲偏移 `"
      << static_cast<uint64_t>(render.anomaly.instance_index) * 32U
      << "`，framebuffer `"
      << (anomaly_frame != nullptr
              ? hex_u64(anomaly_frame->draw.framebuffer)
              : "0x0000000000000000")
      << "` 的首差异 RGBA 偏移 `" << report.diff.first_differing_raw_offset
      << "`，差异包围盒 `(" << report.diff.bounds.min_x << ","
      << report.diff.bounds.min_y << ")-(" << report.diff.bounds.max_x << ","
      << report.diff.bounds.max_y << ")`，实例屏幕单元 `("
      << (target_placement != nullptr ? target_placement->x0 : 0U) << ","
      << (target_placement != nullptr ? target_placement->y0 : 0U) << ")-("
      << (target_placement != nullptr ? target_placement->x1 : 0U) << ","
      << (target_placement != nullptr ? target_placement->y1 : 0U) << ")`。\n\n"
      << "## 画面差异\n\n"
      << "- 差异像素：`" << report.diff.differing_pixels << "`\n"
      << "- 差异字节：`" << report.diff.differing_bytes << "`\n"
      << "- 最大通道差：`" << report.diff.max_channel_delta << "`\n"
      << "- 平均通道差：`" << report.diff.mean_channel_delta << "`\n"
      << "- 差异包围盒：`(" << report.diff.bounds.min_x << ", "
      << report.diff.bounds.min_y << ") - (" << report.diff.bounds.max_x
      << ", " << report.diff.bounds.max_y << ")`\n"
      << "- 首差异像素：`" << report.diff.first_differing_pixel
      << "`，RGBA 原始偏移：`" << report.diff.first_differing_raw_offset
      << "`\n\n"
      << "## 能量对照\n\n"
      << "| 实例 | 输入能量积分 | 基线平均亮度 | 异常平均亮度 | 相对变化 | 屏幕单元 |\n"
      << "|---:|---:|---:|---:|---:|---|\n";
  for (const EnergySample& sample : report.energy_samples) {
    out << "| " << sample.instance_index << " | " << sample.expected_energy
        << " | " << sample.baseline_mean_luma << " | "
        << sample.anomaly_mean_luma << " | " << sample.relative_change
        << " | (" << sample.x0 << "," << sample.y0 << ")-(" << sample.x1
        << "," << sample.y1 << ") |\n";
  }
  out << "\n## 空输入与超大输入\n\n"
      << "| 用例 | UTC 时间 | 文件大小 | 上限 | 错误码 | 原始偏移 | 观测/上限 |\n"
      << "|---|---|---:|---:|---|---:|---|\n";
  for (const InvalidInputEvidence& invalid : report.invalid_inputs) {
    out << "| `" << invalid.case_name << "` | `" << invalid.checked_at_utc
        << "` | " << invalid.file_size << " | " << invalid.max_bytes << " | `"
        << invalid.error_code << "` | " << invalid.raw_offset << " | "
        << invalid.observed << "/" << invalid.limit << " |\n";
  }
  out << "\n## 验收检查\n\n"
      << "| 检查项 | 结果 | 期望 | 实际 |\n"
      << "|---|---|---|---|\n";
  for (const AcceptanceCheck& check : report.checks) {
    out << "| `" << check.id << "` | "
        << (check.passed ? "PASS" : "FAIL") << " | `" << check.expected
        << "` | `" << check.actual << "` |\n";
  }
  out << "\n## 关键产物\n\n";
  for (const ArtifactEvidence& artifact : report.artifacts) {
    out << "- `" << artifact.role << "`: `" << artifact.path << "` ("
        << artifact.size << " bytes, SHA-256 `" << artifact.sha256 << "`)\n";
  }
  return write_text(path, out.str(), error);
}

}  // namespace msphere
