#include "rtsync/trace.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace rtsync {
namespace {

std::string get_string(const Json& object, const char* key,
                       const std::string& fallback = "") {
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) return fallback;
  return it->get<std::string>();
}

std::int64_t get_i64(const Json& object, const char* key,
                     std::int64_t fallback = 0) {
  const auto it = object.find(key);
  if (it == object.end()) return fallback;
  if (it->is_number_integer()) return it->get<std::int64_t>();
  if (it->is_number_unsigned()) {
    const auto value = it->get<std::uint64_t>();
    return value > static_cast<std::uint64_t>(
                       std::numeric_limits<std::int64_t>::max())
               ? std::numeric_limits<std::int64_t>::max()
               : static_cast<std::int64_t>(value);
  }
  if (it->is_number_float()) {
    const double value = it->get<double>();
    if (value > static_cast<double>(
                    std::numeric_limits<std::int64_t>::max())) {
      return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(std::llround(value));
  }
  return fallback;
}

std::uint64_t get_u64(const Json& object, const char* key,
                      std::uint64_t fallback = 0) {
  const auto it = object.find(key);
  if (it == object.end()) return fallback;
  if (it->is_number_unsigned()) return it->get<std::uint64_t>();
  if (it->is_number_integer()) {
    const auto value = it->get<std::int64_t>();
    return value < 0 ? fallback : static_cast<std::uint64_t>(value);
  }
  if (it->is_number_float()) {
    const double value = it->get<double>();
    if (value < 0.0) return fallback;
    if (value > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
      return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(std::llround(value));
  }
  return fallback;
}

std::vector<std::string> get_strings(const Json& object, const char* key) {
  std::vector<std::string> result;
  const auto it = object.find(key);
  if (it == object.end() || !it->is_array()) return result;
  for (const Json& value : *it) {
    if (value.is_string()) result.push_back(value.get<std::string>());
  }
  return result;
}

const Json& array_at(const Json& root, const char* key) {
  static const Json empty = Json::array();
  const auto it = root.find(key);
  return it != root.end() && it->is_array() ? *it : empty;
}

}  // namespace

bool parse_trace(const Json& root, Trace* trace, std::string* error) {
  if (!root.is_object()) {
    if (error) *error = "trace root must be a JSON object";
    return false;
  }
  *trace = Trace{};
  trace->schema = get_string(root, "schema", kTraceSchema);
  if (trace->schema != kTraceSchema) {
    trace->warnings.push_back("unexpected schema: " + trace->schema);
  } else {
    trace->warnings.clear();
  }
  if (const auto it = root.find("capture");
      it != root.end() && it->is_object()) {
    trace->capture.id = get_string(*it, "id");
    trace->capture.scenario = get_string(*it, "scenario");
    trace->capture.started_utc = get_string(*it, "started_utc");
    trace->capture.description = get_string(*it, "description");
  }

  for (const Json& item : array_at(root, "devices")) {
    if (!item.is_object()) continue;
    Device device;
    device.id = get_string(item, "id");
    device.name = get_string(item, "name");
    device.clock_frequency_hz =
        get_u64(item, "clock_frequency_hz", 1000000000ull);
    const auto clock = item.find("clock");
    const Json& clock_object =
        clock != item.end() && clock->is_object() ? *clock : item;
    if (clock_object.contains("frequency_hz")) {
      device.clock_frequency_hz =
          get_u64(clock_object, "frequency_hz", device.clock_frequency_hz);
    }
    for (const Json& point : array_at(clock_object, "calibration")) {
      if (!point.is_object()) continue;
      ClockPoint cal;
      cal.host_ns = get_i64(point, "host_ns");
      cal.device_ticks = get_i64(point, "device_ticks");
      device.calibration.push_back(cal);
    }
    trace->devices.push_back(std::move(device));
  }

  for (const Json& item : array_at(root, "render_threads")) {
    if (!item.is_object()) continue;
    RenderThread thread;
    thread.id = get_string(item, "id");
    thread.device_id = get_string(item, "device_id");
    thread.os_thread_id = get_u64(item, "os_thread_id");
    for (const Json& event_json : array_at(item, "events")) {
      if (!event_json.is_object()) continue;
      ThreadEvent event;
      event.kind = get_string(event_json, "kind");
      event.target = get_string(event_json, "target");
      event.state = get_string(event_json, "state");
      event.host_ns = get_i64(event_json, "host_ns");
      event.device_ticks = get_i64(event_json, "device_ticks");
      thread.events.push_back(std::move(event));
    }
    trace->render_threads.push_back(std::move(thread));
  }

  for (const Json& item : array_at(root, "command_buffers")) {
    if (!item.is_object()) continue;
    CommandBuffer buffer;
    buffer.id = get_string(item, "id");
    buffer.device_id = get_string(item, "device_id");
    buffer.thread_id = get_string(item, "thread_id");
    buffer.fence_id = get_string(item, "fence_id");
    buffer.state = get_string(item, "state");
    for (const Json& op_json : array_at(item, "operations")) {
      if (!op_json.is_object()) continue;
      CommandOp op;
      op.seq = static_cast<std::uint32_t>(get_u64(op_json, "seq"));
      op.op = get_string(op_json, "op");
      op.target = get_string(op_json, "target");
      op.draw_id = get_string(op_json, "draw_id");
      op.resource_id = get_string(op_json, "resource_id");
      op.from_state = get_string(op_json, "from_state");
      op.to_state = get_string(op_json, "to_state");
      op.host_ns = get_i64(op_json, "host_ns");
      op.device_ticks = get_i64(op_json, "device_ticks");
      buffer.operations.push_back(std::move(op));
    }
    trace->command_buffers.push_back(std::move(buffer));
  }

  const auto parse_fence_time = [](const Json& parent, const char* key) {
    FenceTime time;
    const auto it = parent.find(key);
    if (it == parent.end() || !it->is_object()) return time;
    time.present = true;
    time.host_ns = get_i64(*it, "host_ns");
    time.device_ticks = get_i64(*it, "device_ticks");
    return time;
  };

  for (const Json& item : array_at(root, "fences")) {
    if (!item.is_object()) continue;
    Fence fence;
    fence.id = get_string(item, "id");
    fence.device_id = get_string(item, "device_id");
    fence.command_buffer_id = get_string(item, "command_buffer_id");
    fence.state = get_string(item, "state");
    fence.signal = parse_fence_time(item, "signal");
    fence.wait = parse_fence_time(item, "wait");
    trace->fences.push_back(std::move(fence));
  }

  for (const Json& item : array_at(root, "draw_calls")) {
    if (!item.is_object()) continue;
    DrawCall draw;
    draw.id = get_string(item, "id");
    draw.device_id = get_string(item, "device_id");
    draw.thread_id = get_string(item, "thread_id");
    draw.command_buffer_id = get_string(item, "command_buffer_id");
    draw.framebuffer_id = get_string(item, "framebuffer_id");
    draw.resource_state = get_string(item, "resource_state");
    draw.resource_ids = get_strings(item, "resource_ids");
    draw.start_device_ticks = get_i64(item, "start_device_ticks");
    draw.end_device_ticks = get_i64(item, "end_device_ticks");
    trace->draw_calls.push_back(std::move(draw));
  }

  for (const Json& item : array_at(root, "resources")) {
    if (!item.is_object()) continue;
    Resource resource;
    resource.id = get_string(item, "id");
    resource.device_id = get_string(item, "device_id");
    resource.kind = get_string(item, "kind");
    resource.initial_state = get_string(item, "initial_state");
    for (const Json& access_json : array_at(item, "accesses")) {
      if (!access_json.is_object()) continue;
      ResourceAccess access;
      access.device_id = get_string(access_json, "device_id");
      access.thread_id = get_string(access_json, "thread_id");
      access.draw_id = get_string(access_json, "draw_id");
      access.mode = get_string(access_json, "mode");
      access.state = get_string(access_json, "state");
      access.start_device_ticks = get_i64(access_json, "start_device_ticks");
      access.end_device_ticks = get_i64(access_json, "end_device_ticks");
      resource.accesses.push_back(std::move(access));
    }
    trace->resources.push_back(std::move(resource));
  }

  for (const Json& item : array_at(root, "framebuffers")) {
    if (!item.is_object()) continue;
    Framebuffer framebuffer;
    framebuffer.id = get_string(item, "id");
    framebuffer.device_id = get_string(item, "device_id");
    framebuffer.width = static_cast<std::uint32_t>(get_u64(item, "width"));
    framebuffer.height = static_cast<std::uint32_t>(get_u64(item, "height"));
    framebuffer.pixel_file = get_string(item, "pixel_file");
    framebuffer.sha256 = get_string(item, "sha256");
    trace->framebuffers.push_back(std::move(framebuffer));
  }

  for (const Json& item : array_at(root, "frames")) {
    if (!item.is_object()) continue;
    Frame frame;
    frame.id = get_string(item, "id");
    frame.group = get_string(item, "group", "default");
    frame.role = get_string(item, "role");
    frame.device_id = get_string(item, "device_id");
    frame.draw_id = get_string(item, "draw_id");
    frame.framebuffer_id = get_string(item, "framebuffer_id");
    frame.image_file = get_string(item, "image_file");
    frame.host_ns = get_i64(item, "host_ns");
    frame.device_ticks = get_i64(item, "device_ticks");
    trace->frames.push_back(std::move(frame));
  }

  trace->record_count = count_trace_records(*trace);
  std::set<std::string> ids;
  const auto check_unique = [&](const std::string& kind,
                                const std::string& id) {
    if (id.empty()) {
      trace->warnings.push_back(kind + " has empty id");
      return;
    }
    if (!ids.insert(kind + ":" + id).second) {
      trace->warnings.push_back("duplicate " + kind + " id: " + id);
    }
  };
  for (const Device& value : trace->devices) check_unique("device", value.id);
  for (const RenderThread& value : trace->render_threads) {
    check_unique("render_thread", value.id);
  }
  for (const CommandBuffer& value : trace->command_buffers) {
    check_unique("command_buffer", value.id);
  }
  for (const Fence& value : trace->fences) check_unique("fence", value.id);
  for (const DrawCall& value : trace->draw_calls) check_unique("draw", value.id);
  for (const Resource& value : trace->resources) {
    check_unique("resource", value.id);
  }
  for (const Framebuffer& value : trace->framebuffers) {
    check_unique("framebuffer", value.id);
  }
  for (const Frame& value : trace->frames) check_unique("frame", value.id);
  return true;
}

std::uint64_t count_trace_records(const Trace& trace) {
  std::uint64_t count = 0;
  count += trace.devices.size();
  count += trace.render_threads.size();
  count += trace.command_buffers.size();
  count += trace.fences.size();
  count += trace.draw_calls.size();
  count += trace.resources.size();
  count += trace.framebuffers.size();
  count += trace.frames.size();
  for (const RenderThread& thread : trace.render_threads) {
    count += thread.events.size();
  }
  for (const CommandBuffer& buffer : trace.command_buffers) {
    count += buffer.operations.size();
  }
  for (const Resource& resource : trace.resources) {
    count += resource.accesses.size();
  }
  return count;
}

Json trace_summary_json(const Trace& trace) {
  Json summary = Json::object();
  summary["devices"] = trace.devices.size();
  summary["render_threads"] = trace.render_threads.size();
  summary["command_buffers"] = trace.command_buffers.size();
  summary["fences"] = trace.fences.size();
  summary["draw_calls"] = trace.draw_calls.size();
  summary["resources"] = trace.resources.size();
  summary["framebuffers"] = trace.framebuffers.size();
  summary["frames"] = trace.frames.size();
  summary["records"] = trace.record_count;
  return summary;
}

}  // namespace rtsync

