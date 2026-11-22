#include "rtsync/analyzer.hpp"
#include "rtsync/clock.hpp"
#include "rtsync/json_util.hpp"
#include "rtsync/sha256.hpp"
#include "rtsync/trace.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    ++failures;
  }
}

}  // namespace

int main() {
  using namespace rtsync;
  check(sha256_hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 known vector");

  Device device;
  device.id = "gpu-b";
  device.clock_frequency_hz = 1000000000ull;
  device.calibration = {{1050000000, 150000000},
                        {1200000000, 300000000},
                        {1350000000, 450000000}};
  const ClockModel clock = fit_clock(device);
  check(clock.valid && clock.calibrated, "clock calibration is valid");
  check(map_device_ticks(clock, 220000000) == 1120000000,
        "raw device ticks map to host nanoseconds");
  check(std::fabs(static_cast<double>(clock.slope_ns_per_tick) - 1.0) < 1e-9,
        "clock slope is one ns per tick");

  const Json ordered = Json::parse("{\"z\":1,\"a\":2,\"m\":3}");
  check(canonical_json(ordered) == "{\"z\":1,\"a\":2,\"m\":3}",
        "ordered_json preserves source field order");

  const Json trace_json = Json::parse(R"({
    "schema":"rtsync.trace.v1",
    "capture":{"id":"unit","scenario":"unit"},
    "devices":[
      {"id":"a","clock_frequency_hz":1000000000,"calibration":[
        {"host_ns":1000,"device_ticks":0},{"host_ns":2000,"device_ticks":1000}]},
      {"id":"b","clock_frequency_hz":1000000000,"calibration":[
        {"host_ns":900,"device_ticks":1000},{"host_ns":1900,"device_ticks":2000}]}
    ],
    "render_threads":[
      {"id":"ta","device_id":"a"},{"id":"tb","device_id":"b"}
    ],
    "command_buffers":[
      {"id":"cba","device_id":"a","thread_id":"ta","fence_id":"fa"},
      {"id":"cbb","device_id":"b","thread_id":"tb","fence_id":"fb"}
    ],
    "fences":[
      {"id":"fa","device_id":"a","command_buffer_id":"cba","state":"signaled",
       "signal":{"host_ns":0,"device_ticks":500},
       "wait":{"host_ns":0,"device_ticks":400}}
    ],
    "draw_calls":[
      {"id":"wa","device_id":"a","thread_id":"ta","command_buffer_id":"cba",
       "resource_state":"TRANSFER_DST_OPTIMAL","resource_ids":["tex"],
       "start_device_ticks":0,"end_device_ticks":600},
      {"id":"rb","device_id":"b","thread_id":"tb","command_buffer_id":"cbb",
       "resource_state":"SHADER_READ_ONLY_OPTIMAL","resource_ids":["tex"],
       "start_device_ticks":1500,"end_device_ticks":1900}
    ],
    "resources":[
      {"id":"tex","device_id":"a","kind":"image","accesses":[
        {"device_id":"a","thread_id":"ta","draw_id":"wa","mode":"write",
         "state":"TRANSFER_DST_OPTIMAL","start_device_ticks":0,
         "end_device_ticks":600},
        {"device_id":"b","thread_id":"tb","draw_id":"rb","mode":"read",
         "state":"SHADER_READ_ONLY_OPTIMAL","start_device_ticks":1500,
         "end_device_ticks":1900}
      ]}
    ]
  })");
  Trace trace;
  std::string error;
  check(parse_trace(trace_json, &trace, &error), "trace parses");
  AnalyzeOptions options;
  options.input_sha256 = sha256_hex(trace_json.dump());
  options.input_bytes = trace_json.dump().size();
  const Analysis analysis = analyze_trace(trace, options);
  bool race = false;
  bool fence = false;
  for (const Finding& finding : analysis.findings) {
    race = race || finding.type == "cross_device_resource_race";
    fence = fence || finding.type == "fence_wait_before_signal";
  }
  check(race, "cross-device overlap is detected");
  check(fence, "fence wait-before-signal is detected");
  check(!analysis.correlations.empty() ||
            analysis.findings.size() >= 2,
        "findings remain available for correlation");
  return failures == 0 ? 0 : 1;
}

