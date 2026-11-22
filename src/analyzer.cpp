#include "rtsync/analyzer.hpp"

#include "rtsync/clock.hpp"
#include "rtsync/image.hpp"
#include "rtsync/json_util.hpp"
#include "rtsync/sha256.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace rtsync {
namespace {

constexpr long double kClockResidualLimitNs = 1000000.0L;

std::string hex_id(std::size_t value) {
  std::ostringstream out;
  out << value;
  return out.str();
}

template <typename T>
void append_unique(std::vector<std::string>* values, const T& items) {
  for (const auto& item : items) {
    const std::string value = item;
    if (value.empty()) continue;
    if (std::find(values->begin(), values->end(), value) == values->end()) {
      values->push_back(value);
    }
  }
}

std::vector<std::string> draw_ids_for_buffer(const Trace& trace,
                                             const std::string& buffer_id) {
  std::vector<std::string> result;
  for (const DrawCall& draw : trace.draw_calls) {
    if (draw.command_buffer_id == buffer_id) result.push_back(draw.id);
  }
  return result;
}

const ClockModel* find_clock(const std::vector<ClockModel>& clocks,
                             const std::string& id) {
  for (const ClockModel& clock : clocks) {
    if (clock.device_id == id) return &clock;
  }
  return nullptr;
}

std::string access_clock_evidence(const ResourceAccess& access) {
  std::ostringstream out;
  out << "device=" << access.device_id << " thread=" << access.thread_id
      << " draw=" << access.draw_id << " mode=" << access.mode
      << " state=" << access.state << " ticks=[" << access.start_device_ticks
      << "," << access.end_device_ticks << "]";
  return out.str();
}

struct MappedAccess {
  const Resource* resource = nullptr;
  const ResourceAccess* access = nullptr;
  TimeSpan span;
};

bool is_write(const std::string& mode) {
  return mode.find("write") != std::string::npos ||
         mode.find("Write") != std::string::npos;
}

std::string overlap_key(const MappedAccess& left, const MappedAccess& right) {
  std::string a = left.access->draw_id + ":" + left.access->device_id + ":" +
                  left.access->thread_id;
  std::string b = right.access->draw_id + ":" + right.access->device_id + ":" +
                  right.access->thread_id;
  if (b < a) std::swap(a, b);
  return left.resource->id + "|" + a + "|" + b;
}

std::string safe_name(const std::string& input) {
  std::string output;
  for (char c : input) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
      output.push_back(c);
    } else {
      output.push_back('_');
    }
  }
  return output.empty() ? "unnamed" : output;
}

std::string resolve_source_path(const std::string& source_dir,
                                const std::string& path) {
  if (path.empty()) return {};
  const fs::path candidate(path);
  if (candidate.is_absolute()) return candidate.string();
  if (source_dir.empty()) return candidate.string();
  return (fs::path(source_dir) / candidate).lexically_normal().string();
}

const Framebuffer* find_framebuffer(const Trace& trace,
                                    const std::string& id) {
  for (const Framebuffer& framebuffer : trace.framebuffers) {
    if (framebuffer.id == id) return &framebuffer;
  }
  return nullptr;
}

void copy_frame_images(Trace* trace, const AnalyzeOptions& options,
                       std::vector<std::string>* errors) {
  if (options.frame_dir.empty()) return;
  std::error_code ec;
  fs::create_directories(options.frame_dir, ec);
  for (Frame& frame : trace->frames) {
    std::string relative_image = frame.image_file;
    if (relative_image.empty()) {
      if (const Framebuffer* framebuffer =
              find_framebuffer(*trace, frame.framebuffer_id)) {
        relative_image = framebuffer->pixel_file;
      }
    }
    if (relative_image.empty()) {
      errors->push_back("frame " + frame.id + " has no image path");
      continue;
    }
    const std::string source =
        resolve_source_path(options.source_dir, relative_image);
    std::string bytes;
    std::string error;
    if (!read_file(source, &bytes, &error)) {
      errors->push_back("frame " + frame.id + ": " + error);
      continue;
    }
    const std::string output_name = safe_name(frame.id) + ".ppm";
    const std::string output =
        (fs::path(options.frame_dir) / output_name).string();
    if (!write_file(output, bytes, &error)) {
      errors->push_back("frame " + frame.id + ": " + error);
      continue;
    }
    frame.image_file = "frames/" + output_name;
  }
}

VisualDiff compare_frames(const std::string& group, const Frame& golden,
                          const Frame& observed,
                          const AnalyzeOptions& options) {
  VisualDiff diff;
  diff.group = group;
  diff.golden_frame_id = golden.id;
  diff.observed_frame_id = observed.id;
  diff.golden_file = golden.image_file;
  diff.observed_file = observed.image_file;
  std::string error;
  const std::string golden_path = golden.image_file.empty()
                                      ? std::string()
                                      : (fs::path(options.frame_dir) /
                                         fs::path(golden.image_file).filename())
                                            .string();
  const std::string observed_path =
      observed.image_file.empty()
          ? std::string()
          : (fs::path(options.frame_dir) /
             fs::path(observed.image_file).filename())
                .string();
  Image golden_image;
  Image observed_image;
  if (!load_ppm(golden_path, &golden_image, &error)) {
    diff.image_error = true;
    diff.error = error;
    return diff;
  }
  if (!load_ppm(observed_path, &observed_image, &error)) {
    diff.image_error = true;
    diff.error = error;
    return diff;
  }
  if (golden_image.width != observed_image.width ||
      golden_image.height != observed_image.height) {
    diff.image_error = true;
    diff.error = "golden and observed image dimensions differ";
    return diff;
  }
  diff.width = golden_image.width;
  diff.height = golden_image.height;
  diff.total_pixels = static_cast<std::uint64_t>(diff.width) * diff.height;
  std::uint64_t channel_sum = 0;
  std::uint32_t x0 = diff.width;
  std::uint32_t y0 = diff.height;
  std::uint32_t x1 = 0;
  std::uint32_t y1 = 0;
  for (std::uint32_t y = 0; y < diff.height; ++y) {
    for (std::uint32_t x = 0; x < diff.width; ++x) {
      const std::size_t index =
          (static_cast<std::size_t>(y) * diff.width + x) * 3;
      std::uint32_t pixel_max = 0;
      for (int channel = 0; channel < 3; ++channel) {
        const std::uint32_t a =
            golden_image.rgb[index + static_cast<std::size_t>(channel)];
        const std::uint32_t b =
            observed_image.rgb[index + static_cast<std::size_t>(channel)];
        const std::uint32_t delta = a > b ? a - b : b - a;
        channel_sum += delta;
        pixel_max = std::max(pixel_max, delta);
      }
      if (pixel_max > 0) {
        ++diff.changed_pixels;
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x + 1);
        y1 = std::max(y1, y + 1);
      }
      diff.max_channel_delta =
          std::max<std::uint64_t>(diff.max_channel_delta, pixel_max);
    }
  }
  if (diff.changed_pixels > 0) {
    diff.bbox_present = true;
    diff.bbox_x0 = x0;
    diff.bbox_y0 = y0;
    diff.bbox_x1 = x1;
    diff.bbox_y1 = y1;
  }
  if (diff.total_pixels > 0) {
    diff.mean_abs_error =
        static_cast<double>(channel_sum) /
        (static_cast<double>(diff.total_pixels) * 3.0);
  }
  diff.golden_sha256 = sha256_file_hex(golden_path, &error);
  diff.observed_sha256 = sha256_file_hex(observed_path, &error);
  const Image diff_image = make_diff_image(golden_image, observed_image);
  const std::string diff_name = safe_name(group) + ".ppm";
  const std::string diff_path =
      (fs::path(options.diff_dir) / diff_name).string();
  std::error_code ec;
  fs::create_directories(options.diff_dir, ec);
  if (!write_ppm(diff_path, diff_image, &error)) {
    diff.image_error = true;
    diff.error = error;
    return diff;
  }
  diff.diff_file = "diffs/" + diff_name;
  diff.diff_sha256 = sha256_file_hex(diff_path, &error);
  return diff;
}

}  // namespace

Analysis analyze_trace(Trace trace, const AnalyzeOptions& options) {
  Analysis result;
  result.input_sha256 = options.input_sha256;
  result.input_bytes = options.input_bytes;
  result.limits = options.limits;
  result.trace = std::move(trace);
  result.empty = result.trace.record_count == 0;
  if (result.empty) {
    result.status = "empty";
    result.status_detail =
        "trace contains no device, thread, command-buffer, fence, draw, "
        "resource, framebuffer or frame records";
    return result;
  }

  std::unordered_map<std::string, ClockModel> clock_by_device;
  for (const Device& device : result.trace.devices) {
    ClockModel clock = fit_clock(device);
    clock_by_device[device.id] = clock;
    result.clocks.push_back(clock);
    if (!clock.valid || !clock.calibrated) {
      Finding finding;
      finding.id = "F-" + hex_id(result.findings.size() + 1);
      finding.type = "uncalibrated_clock_domain";
      finding.severity = "high";
      finding.message =
          "device clock cannot be mapped reliably to the host timeline";
      finding.raw_clock_evidence = "device=" + device.id + " points=" +
                                   std::to_string(device.calibration.size());
      finding.normalized_clock_evidence = clock.error;
      result.findings.push_back(std::move(finding));
    } else if (clock.max_residual_ns > kClockResidualLimitNs) {
      Finding finding;
      finding.id = "F-" + hex_id(result.findings.size() + 1);
      finding.type = "clock_calibration_residual";
      finding.severity = "medium";
      finding.message =
          "least-squares clock mapping leaves a residual above 1 ms";
      finding.raw_clock_evidence = "device=" + device.id + " points=" +
                                   std::to_string(device.calibration.size());
      finding.normalized_clock_evidence =
          "max_residual_ns=" + std::to_string(
                                   static_cast<std::int64_t>(
                                       std::llround(clock.max_residual_ns)));
      result.findings.push_back(std::move(finding));
    }
  }

  std::unordered_map<std::string, const CommandBuffer*> buffer_by_id;
  for (const CommandBuffer& buffer : result.trace.command_buffers) {
    buffer_by_id[buffer.id] = &buffer;
  }
  std::unordered_map<std::string, const Fence*> fence_by_id;
  for (const Fence& fence : result.trace.fences) fence_by_id[fence.id] = &fence;

  for (const Fence& fence : result.trace.fences) {
    const ClockModel* clock = find_clock(result.clocks, fence.device_id);
    const auto buffer_it = buffer_by_id.find(fence.command_buffer_id);
    if (buffer_it == buffer_by_id.end()) {
      Finding finding;
      finding.id = "F-" + hex_id(result.findings.size() + 1);
      finding.type = "missing_command_buffer";
      finding.severity = "medium";
      finding.message = "fence references a command buffer that is absent";
      finding.fence_ids.push_back(fence.id);
      finding.raw_clock_evidence =
          "command_buffer_id=" + fence.command_buffer_id;
      result.findings.push_back(std::move(finding));
      continue;
    }
    const CommandBuffer& buffer = *buffer_it->second;
    if (clock != nullptr && !buffer.device_id.empty() &&
        buffer.device_id != fence.device_id) {
      Finding finding;
      finding.id = "F-" + hex_id(result.findings.size() + 1);
      finding.type = "fence_device_mismatch";
      finding.severity = "high";
      finding.message =
          "fence and command buffer belong to different logical devices";
      finding.fence_ids.push_back(fence.id);
      finding.command_buffer_ids.push_back(buffer.id);
      finding.draw_ids = draw_ids_for_buffer(result.trace, buffer.id);
      finding.raw_clock_evidence =
          "fence_device=" + fence.device_id +
          " buffer_device=" + buffer.device_id;
      result.findings.push_back(std::move(finding));
    }
    if (!fence.wait.present) continue;
    const std::int64_t wait_ns =
        clock ? map_device_ticks(*clock, fence.wait.device_ticks)
              : fence.wait.host_ns;
    const bool missing_signal = !fence.signal.present;
    const std::int64_t signal_ns =
        fence.signal.present
            ? (clock ? map_device_ticks(*clock, fence.signal.device_ticks)
                     : fence.signal.host_ns)
            : wait_ns;
    if (missing_signal || fence.state == "unsignaled" ||
        fence.state == "reset" || wait_ns < signal_ns) {
      Finding finding;
      finding.id = "F-" + hex_id(result.findings.size() + 1);
      finding.type = missing_signal ? "fence_not_signaled_before_wait"
                                    : "fence_wait_before_signal";
      finding.severity = "critical";
      finding.message = missing_signal
                            ? "render thread waited on a fence with no signal "
                              "observation"
                            : "render thread observed fence wait before its "
                              "signal on the normalized timeline";
      finding.fence_ids.push_back(fence.id);
      finding.command_buffer_ids.push_back(buffer.id);
      finding.draw_ids = draw_ids_for_buffer(result.trace, buffer.id);
      finding.raw_clock_evidence =
          "fence=" + fence.id +
          " signal_ticks=" +
          (fence.signal.present
               ? std::to_string(fence.signal.device_ticks)
               : std::string("<absent>")) +
          " wait_ticks=" + std::to_string(fence.wait.device_ticks) +
          " state=" + fence.state;
      finding.normalized_clock_evidence =
          "signal_host_ns=" + std::to_string(signal_ns) +
          " wait_host_ns=" + std::to_string(wait_ns) +
          " delta_ns=" + std::to_string(wait_ns - signal_ns);
      result.findings.push_back(std::move(finding));
    }
  }

  std::set<std::string> overlap_keys;
  bool finding_limit_hit = false;
  for (const Resource& resource : result.trace.resources) {
    std::vector<MappedAccess> accesses;
    accesses.reserve(resource.accesses.size());
    for (const ResourceAccess& access : resource.accesses) {
      const ClockModel* clock = find_clock(result.clocks, access.device_id);
      if (clock == nullptr) {
        clock = find_clock(result.clocks, resource.device_id);
      }
      ClockModel identity;
      identity.valid = false;
      MappedAccess mapped;
      mapped.resource = &resource;
      mapped.access = &access;
      mapped.span = map_span(clock ? *clock : identity,
                             access.start_device_ticks,
                             access.end_device_ticks);
      accesses.push_back(mapped);
    }
    std::sort(accesses.begin(), accesses.end(),
              [](const MappedAccess& left, const MappedAccess& right) {
                if (left.span.start_ns != right.span.start_ns) {
                  return left.span.start_ns < right.span.start_ns;
                }
                if (left.span.end_ns != right.span.end_ns) {
                  return left.span.end_ns < right.span.end_ns;
                }
                if (left.access->draw_id != right.access->draw_id) {
                  return left.access->draw_id < right.access->draw_id;
                }
                return left.access->device_id < right.access->device_id;
              });
    std::vector<MappedAccess> active;
    for (const MappedAccess& current : accesses) {
      active.erase(
          std::remove_if(active.begin(), active.end(),
                         [&](const MappedAccess& value) {
                           return value.span.end_ns < current.span.start_ns;
                         }),
          active.end());
      for (const MappedAccess& previous : active) {
        if (!spans_overlap(previous.span, current.span)) continue;
        const bool different_executor =
            previous.access->device_id != current.access->device_id ||
            previous.access->thread_id != current.access->thread_id;
        if (!different_executor) continue;
        if (!is_write(previous.access->mode) &&
            !is_write(current.access->mode)) {
          continue;
        }
        const std::string key = overlap_key(previous, current);
        if (!overlap_keys.insert(key).second) continue;
        if (result.findings.size() + 1 >= result.limits.max_findings) {
          finding_limit_hit = true;
          break;
        }
        const MappedAccess& writer =
            is_write(previous.access->mode) ? previous : current;
        const MappedAccess& other = &writer == &previous ? current : previous;
        const std::int64_t overlap_ns =
            std::min(previous.span.end_ns, current.span.end_ns) -
            std::max(previous.span.start_ns, current.span.start_ns);
        Finding finding;
        finding.id = "F-" + hex_id(result.findings.size() + 1);
        const bool cross_device =
            previous.access->device_id != current.access->device_id;
        finding.type = cross_device ? "cross_device_resource_race"
                                    : "cross_thread_resource_race";
        finding.severity = "critical";
        finding.resource_ids.push_back(resource.id);
        finding.draw_ids.push_back(writer.access->draw_id);
        finding.draw_ids.push_back(other.access->draw_id);
        finding.resource_state =
            writer.access->state + " / " + other.access->state;
        finding.message =
            "overlapping write and access use incompatible resource states "
            "on unsynchronized executors";
        finding.raw_clock_evidence =
            "resource=" + resource.id +
            " A={" + access_clock_evidence(*previous.access) + "}" +
            " B={" + access_clock_evidence(*current.access) + "}";
        finding.normalized_clock_evidence =
            "A_host_ns=[" + std::to_string(previous.span.start_ns) + "," +
            std::to_string(previous.span.end_ns) + "]" +
            " B_host_ns=[" + std::to_string(current.span.start_ns) + "," +
            std::to_string(current.span.end_ns) + "]" +
            " overlap_ns=" + std::to_string(overlap_ns);
        result.findings.push_back(std::move(finding));
      }
      if (finding_limit_hit) break;
      active.push_back(current);
    }
    if (finding_limit_hit) break;
  }
  if (finding_limit_hit &&
      result.findings.size() < result.limits.max_findings) {
    Finding finding;
    finding.id = "F-" + hex_id(result.findings.size() + 1);
    finding.type = "finding_limit_reached";
    finding.severity = "medium";
    finding.message =
        "analysis stopped collecting findings at the configured limit";
    finding.raw_clock_evidence =
        "max_findings=" + std::to_string(result.limits.max_findings);
    result.findings.push_back(std::move(finding));
  }

  std::vector<std::string> image_errors;
  copy_frame_images(&result.trace, options, &image_errors);
  for (const std::string& image_error : image_errors) {
    result.trace.warnings.push_back("frame copy: " + image_error);
  }
  std::map<std::string, std::vector<const Frame*>> groups;
  for (const Frame& frame : result.trace.frames) {
    groups[frame.group.empty() ? "default" : frame.group].push_back(&frame);
  }
  for (auto& entry : groups) {
    std::sort(entry.second.begin(), entry.second.end(),
              [](const Frame* left, const Frame* right) {
                return left->id < right->id;
              });
    const Frame* golden = nullptr;
    const Frame* observed = nullptr;
    for (const Frame* frame : entry.second) {
      if (golden == nullptr && frame->role == "golden") golden = frame;
      if (observed == nullptr && frame->role == "observed") observed = frame;
    }
    if (golden == nullptr || observed == nullptr) {
      Finding finding;
      finding.id = "F-" + hex_id(result.findings.size() + 1);
      finding.type = "frame_comparison_incomplete";
      finding.severity = "low";
      finding.message =
          "frame group does not contain both golden and observed roles";
      finding.raw_clock_evidence =
          "group=" + entry.first + " frames=" +
          std::to_string(entry.second.size());
      for (const Frame* frame : entry.second) {
        finding.frame_ids.push_back(frame->id);
      }
      result.findings.push_back(std::move(finding));
      continue;
    }
    VisualDiff diff = compare_frames(entry.first, *golden, *observed, options);
    diff.id = "D-" + hex_id(result.diffs.size() + 1);
    const std::string diff_id = diff.id;
    if (diff.image_error) {
      Finding finding;
      finding.id = "F-" + hex_id(result.findings.size() + 1);
      finding.type = "frame_evidence_unreadable";
      finding.severity = "high";
      finding.message = "golden/observed frame evidence cannot be compared";
      finding.frame_ids = {golden->id, observed->id};
      finding.draw_ids = {golden->draw_id, observed->draw_id};
      finding.normalized_clock_evidence = diff.error;
      result.findings.push_back(std::move(finding));
    } else {
      Finding finding;
      finding.id = "F-" + hex_id(result.findings.size() + 1);
      finding.type = "frame_mismatch";
      finding.severity = diff.changed_pixels == 0 ? "info" : "high";
      finding.message =
          diff.changed_pixels == 0
              ? "golden and observed framebuffers are pixel-identical"
              : "observed framebuffer differs from the synchronized golden "
                "framebuffer";
      finding.frame_ids = {golden->id, observed->id};
      finding.draw_ids = {golden->draw_id, observed->draw_id};
      finding.normalized_clock_evidence =
          "diff=" + diff_id + " changed_pixels=" +
          std::to_string(diff.changed_pixels) + "/" +
          std::to_string(diff.total_pixels) + " mean_abs_error=" +
          std::to_string(diff.mean_abs_error);
      result.findings.push_back(std::move(finding));
    }
    result.diffs.push_back(std::move(diff));
  }

  for (const Finding& finding : result.findings) {
    Correlation correlation;
    correlation.finding_id = finding.id;
    correlation.draw_ids = finding.draw_ids;
    correlation.resource_ids = finding.resource_ids;
    correlation.resource_state = finding.resource_state;
    correlation.fence_ids = finding.fence_ids;
    correlation.command_buffer_ids = finding.command_buffer_ids;
    std::set<std::string> frame_ids;
    for (const std::string& id : finding.frame_ids) frame_ids.insert(id);
    for (const auto& group : groups) {
      bool group_matches = false;
      for (const Frame* frame : group.second) {
        if (frame_ids.count(frame->id) != 0) group_matches = true;
        for (const std::string& draw_id : finding.draw_ids) {
          if (!draw_id.empty() && frame->draw_id == draw_id) {
            group_matches = true;
          }
        }
      }
      if (!group_matches) continue;
      for (const Frame* frame : group.second) {
        append_unique(&correlation.frame_ids,
                      std::vector<std::string>{frame->id});
        append_unique(&correlation.framebuffer_ids,
                      std::vector<std::string>{frame->framebuffer_id});
      }
      for (const VisualDiff& diff : result.diffs) {
        if (diff.group == group.first) {
          append_unique(&correlation.diff_ids,
                        std::vector<std::string>{diff.id});
        }
      }
    }
    if (correlation.draw_ids.empty() && correlation.frame_ids.empty() &&
        correlation.resource_ids.empty() && correlation.fence_ids.empty()) {
      continue;
    }
    correlation.id = "C-" + hex_id(result.correlations.size() + 1);
    result.correlations.push_back(std::move(correlation));
  }

  return result;
}

const Finding* find_finding(const Analysis& analysis, const std::string& id) {
  for (const Finding& finding : analysis.findings) {
    if (finding.id == id) return &finding;
  }
  return nullptr;
}

}  // namespace rtsync

