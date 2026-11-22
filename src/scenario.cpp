#include "rtsync/scenario.hpp"

#include "rtsync/json_util.hpp"
#include "rtsync/sha256.hpp"
#include "rtsync/trace.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace rtsync {

bool write_demo_trace(const ScenarioOptions& options, Trace* trace,
                      std::string* error) {
  *trace = Trace{};
  trace->capture.id = "capture-unsynced-two-device-001";
  trace->capture.scenario = "two-device-unsynchronized-clock";
  trace->capture.started_utc = "2026-11-22T00:00:00Z";
  trace->capture.description =
      "GPU-A writes a shared texture while GPU-B reads it. The devices use "
      "independent ticks; raw ticks look ordered, but calibration maps them "
      "onto an overlapping host interval.";

  Device gpu_a;
  gpu_a.id = "gpu-a";
  gpu_a.name = "producer-device";
  gpu_a.clock_frequency_hz = 1000000000ull;
  gpu_a.calibration = {{1000000000, 0},
                       {1100000000, 100000000},
                       {1200000000, 200000000}};
  trace->devices.push_back(gpu_a);

  Device gpu_b;
  gpu_b.id = "gpu-b";
  gpu_b.name = "consumer-device";
  gpu_b.clock_frequency_hz = 1000000000ull;
  gpu_b.calibration = {{1050000000, 150000000},
                       {1200000000, 300000000},
                       {1350000000, 450000000}};
  trace->devices.push_back(gpu_b);

  RenderThread reader;
  reader.id = "rt-b";
  reader.device_id = "gpu-b";
  reader.os_thread_id = 4102;
  reader.events = {
      {"submit", "cb-b", "pending", 1120000000, 220000000},
      {"wait", "f-b", "waiting", 1150000000, 250000000},
      {"submit", "cb-b", "reused-before-signal", 1160000000, 260000000}};
  trace->render_threads.push_back(reader);
  RenderThread writer;
  writer.id = "rt-a";
  writer.device_id = "gpu-a";
  writer.os_thread_id = 4101;
  writer.events = {
      {"submit", "cb-a", "pending", 1100000000, 100000000},
      {"wait", "f-a", "waiting", 1160000000, 160000000}};
  trace->render_threads.insert(trace->render_threads.begin(), writer);

  CommandBuffer buffer_a;
  buffer_a.id = "cb-a";
  buffer_a.device_id = "gpu-a";
  buffer_a.thread_id = "rt-a";
  buffer_a.fence_id = "f-a";
  buffer_a.state = "completed";
  buffer_a.operations = {
      {1, "transition", "tex-shared", "", "tex-shared", "UNDEFINED",
       "TRANSFER_DST_OPTIMAL", 1100000000, 100000000},
      {2, "draw", "draw-a-write", "draw-a-write", "tex-shared",
       "TRANSFER_DST_OPTIMAL", "TRANSFER_DST_OPTIMAL", 1120000000,
       120000000},
      {3, "transition", "tex-shared", "", "tex-shared",
       "TRANSFER_DST_OPTIMAL", "SHADER_READ_ONLY_OPTIMAL", 1150000000,
       150000000}};
  trace->command_buffers.push_back(buffer_a);
  CommandBuffer buffer_b;
  buffer_b.id = "cb-b";
  buffer_b.device_id = "gpu-b";
  buffer_b.thread_id = "rt-b";
  buffer_b.fence_id = "f-b";
  buffer_b.state = "pending";
  buffer_b.operations = {
      {1, "draw", "draw-b-read", "draw-b-read", "tex-shared",
       "SHADER_READ_ONLY_OPTIMAL", "SHADER_READ_ONLY_OPTIMAL", 1120000000,
       220000000}};
  trace->command_buffers.push_back(buffer_b);

  Fence fence_a;
  fence_a.id = "f-a";
  fence_a.device_id = "gpu-a";
  fence_a.command_buffer_id = "cb-a";
  fence_a.state = "signaled";
  fence_a.signal = {true, 1140000000, 140000000};
  fence_a.wait = {true, 1160000000, 160000000};
  trace->fences.push_back(fence_a);
  Fence fence_b;
  fence_b.id = "f-b";
  fence_b.device_id = "gpu-b";
  fence_b.command_buffer_id = "cb-b";
  fence_b.state = "signaled";
  fence_b.signal = {true, 1180000000, 280000000};
  fence_b.wait = {true, 1150000000, 250000000};
  trace->fences.push_back(fence_b);

  DrawCall draw_a;
  draw_a.id = "draw-a-write";
  draw_a.device_id = "gpu-a";
  draw_a.thread_id = "rt-a";
  draw_a.command_buffer_id = "cb-a";
  draw_a.framebuffer_id = "fb-golden";
  draw_a.resource_state = "TRANSFER_DST_OPTIMAL";
  draw_a.resource_ids = {"tex-shared"};
  draw_a.start_device_ticks = 100000000;
  draw_a.end_device_ticks = 150000000;
  trace->draw_calls.push_back(draw_a);
  DrawCall draw_b;
  draw_b.id = "draw-b-read";
  draw_b.device_id = "gpu-b";
  draw_b.thread_id = "rt-b";
  draw_b.command_buffer_id = "cb-b";
  draw_b.framebuffer_id = "fb-observed";
  draw_b.resource_state = "SHADER_READ_ONLY_OPTIMAL";
  draw_b.resource_ids = {"tex-shared"};
  draw_b.start_device_ticks = 220000000;
  draw_b.end_device_ticks = 270000000;
  trace->draw_calls.push_back(draw_b);

  Resource texture;
  texture.id = "tex-shared";
  texture.device_id = "gpu-a";
  texture.kind = "image";
  texture.initial_state = "UNDEFINED";
  texture.accesses = {
      {"gpu-a", "rt-a", "draw-a-write", "write", "TRANSFER_DST_OPTIMAL",
       100000000, 150000000},
      {"gpu-b", "rt-b", "draw-b-read", "read", "SHADER_READ_ONLY_OPTIMAL",
       220000000, 270000000}};
  trace->resources.push_back(texture);

  Framebuffer golden_fb;
  golden_fb.id = "fb-golden";
  golden_fb.device_id = "gpu-a";
  golden_fb.width = 320;
  golden_fb.height = 200;
  golden_fb.pixel_file = options.golden_image;
  golden_fb.sha256 =
      sha256_file_hex((fs::path(options.output_dir) / options.golden_image)
                          .string());
  trace->framebuffers.push_back(golden_fb);
  Framebuffer observed_fb;
  observed_fb.id = "fb-observed";
  observed_fb.device_id = "gpu-b";
  observed_fb.width = 320;
  observed_fb.height = 200;
  observed_fb.pixel_file = options.observed_image;
  observed_fb.sha256 =
      sha256_file_hex((fs::path(options.output_dir) / options.observed_image)
                          .string());
  trace->framebuffers.push_back(observed_fb);

  Frame golden;
  golden.id = "frame-golden";
  golden.group = "race-001";
  golden.role = "golden";
  golden.device_id = "gpu-a";
  golden.draw_id = "draw-a-write";
  golden.framebuffer_id = "fb-golden";
  golden.image_file = options.golden_image;
  golden.host_ns = 1150000000;
  golden.device_ticks = 150000000;
  trace->frames.push_back(golden);
  Frame observed;
  observed.id = "frame-observed";
  observed.group = "race-001";
  observed.role = "observed";
  observed.device_id = "gpu-b";
  observed.draw_id = "draw-b-read";
  observed.framebuffer_id = "fb-observed";
  observed.image_file = options.observed_image;
  observed.host_ns = 1160000000;
  observed.device_ticks = 260000000;
  trace->frames.push_back(observed);
  trace->record_count = count_trace_records(*trace);

  const fs::path trace_path = fs::path(options.output_dir) / "trace.json";
  Json root = Json::object();
  root["schema"] = trace->schema;
  Json capture = Json::object();
  capture["id"] = trace->capture.id;
  capture["scenario"] = trace->capture.scenario;
  capture["started_utc"] = trace->capture.started_utc;
  capture["description"] = trace->capture.description;
  root["capture"] = std::move(capture);
  root["devices"] = Json::array();
  for (const Device& device : trace->devices) {
    Json value = Json::object();
    value["id"] = device.id;
    value["name"] = device.name;
    value["clock_frequency_hz"] = device.clock_frequency_hz;
    value["calibration"] = Json::array();
    for (const ClockPoint& point : device.calibration) {
      value["calibration"].push_back(
          Json{{"host_ns", point.host_ns},
               {"device_ticks", point.device_ticks}});
    }
    root["devices"].push_back(std::move(value));
  }
  root["render_threads"] = Json::array();
  for (const RenderThread& thread : trace->render_threads) {
    Json value = Json::object();
    value["id"] = thread.id;
    value["device_id"] = thread.device_id;
    value["os_thread_id"] = thread.os_thread_id;
    value["events"] = Json::array();
    for (const ThreadEvent& event : thread.events) {
      value["events"].push_back(
          Json{{"kind", event.kind},
               {"target", event.target},
               {"state", event.state},
               {"host_ns", event.host_ns},
               {"device_ticks", event.device_ticks}});
    }
    root["render_threads"].push_back(std::move(value));
  }
  root["command_buffers"] = Json::array();
  for (const CommandBuffer& buffer : trace->command_buffers) {
    Json value = Json::object();
    value["id"] = buffer.id;
    value["device_id"] = buffer.device_id;
    value["thread_id"] = buffer.thread_id;
    value["fence_id"] = buffer.fence_id;
    value["state"] = buffer.state;
    value["operations"] = Json::array();
    for (const CommandOp& op : buffer.operations) {
      value["operations"].push_back(
          Json{{"seq", op.seq},
               {"op", op.op},
               {"target", op.target},
               {"draw_id", op.draw_id},
               {"resource_id", op.resource_id},
               {"from_state", op.from_state},
               {"to_state", op.to_state},
               {"host_ns", op.host_ns},
               {"device_ticks", op.device_ticks}});
    }
    root["command_buffers"].push_back(std::move(value));
  }
  root["fences"] = Json::array();
  for (const Fence& fence : trace->fences) {
    Json value = Json::object();
    value["id"] = fence.id;
    value["device_id"] = fence.device_id;
    value["command_buffer_id"] = fence.command_buffer_id;
    value["state"] = fence.state;
    value["signal"] = Json{{"host_ns", fence.signal.host_ns},
                           {"device_ticks", fence.signal.device_ticks}};
    value["wait"] = Json{{"host_ns", fence.wait.host_ns},
                         {"device_ticks", fence.wait.device_ticks}};
    root["fences"].push_back(std::move(value));
  }
  root["draw_calls"] = Json::array();
  for (const DrawCall& draw : trace->draw_calls) {
    root["draw_calls"].push_back(
        Json{{"id", draw.id},
             {"device_id", draw.device_id},
             {"thread_id", draw.thread_id},
             {"command_buffer_id", draw.command_buffer_id},
             {"framebuffer_id", draw.framebuffer_id},
             {"resource_state", draw.resource_state},
             {"resource_ids", draw.resource_ids},
             {"start_device_ticks", draw.start_device_ticks},
             {"end_device_ticks", draw.end_device_ticks}});
  }
  root["resources"] = Json::array();
  for (const Resource& resource : trace->resources) {
    Json value = Json::object();
    value["id"] = resource.id;
    value["device_id"] = resource.device_id;
    value["kind"] = resource.kind;
    value["initial_state"] = resource.initial_state;
    value["accesses"] = Json::array();
    for (const ResourceAccess& access : resource.accesses) {
      value["accesses"].push_back(
          Json{{"device_id", access.device_id},
               {"thread_id", access.thread_id},
               {"draw_id", access.draw_id},
               {"mode", access.mode},
               {"state", access.state},
               {"start_device_ticks", access.start_device_ticks},
               {"end_device_ticks", access.end_device_ticks}});
    }
    root["resources"].push_back(std::move(value));
  }
  root["framebuffers"] = Json::array();
  for (const Framebuffer& framebuffer : trace->framebuffers) {
    root["framebuffers"].push_back(
        Json{{"id", framebuffer.id},
             {"device_id", framebuffer.device_id},
             {"width", framebuffer.width},
             {"height", framebuffer.height},
             {"pixel_file", framebuffer.pixel_file},
             {"sha256", framebuffer.sha256}});
  }
  root["frames"] = Json::array();
  for (const Frame& frame : trace->frames) {
    root["frames"].push_back(
        Json{{"id", frame.id},
             {"group", frame.group},
             {"role", frame.role},
             {"device_id", frame.device_id},
             {"draw_id", frame.draw_id},
             {"framebuffer_id", frame.framebuffer_id},
             {"image_file", frame.image_file},
             {"host_ns", frame.host_ns},
             {"device_ticks", frame.device_ticks}});
  }
  return save_json(trace_path.string(), root, error);
}

}  // namespace rtsync

