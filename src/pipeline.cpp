#include "pipeline.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include "material.hpp"
#include "report.hpp"
#include "sha256.hpp"
#include "util.hpp"
#include "verify_core.hpp"
#include "verify_vk.hpp"

namespace nmts {
namespace {

class CpuBackend : public IBackend {
 public:
  explicit CpuBackend(bool inject_failure = false)
      : inject_failure_(inject_failure) {}
  std::uint32_t kind() const override { return kBackendCpu; }
  bool verify_range(const Material& m, std::uint32_t first,
                    std::uint32_t count,
                    std::vector<ResultRecord>& out,
                    std::string& err) override {
    if (inject_failure_ && count > 4) {
      err = "simulated transient memory pressure (chunk > 4)";
      return false;
    }
    (void)err;
    out.clear();
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      std::uint32_t global = first + i;
      const InstanceRecord& q = m.instances[global];
      VerifyInput in;
      in.instance = q;
      std::size_t poff = std::size_t(q.pixel_index) * kPixelSize;
      std::memcpy(in.pixel, m.pixels.data() + poff, 4);
      in.eps = {m.header.eps_orthogonal, m.header.eps_unit,
                m.header.eps_normal_z};
      ResultRecord r = verify_one(global, in);
      r.backend = kBackendCpu;
      r.set_instance_offset(
          m.header.instance_offset +
          std::uint64_t(global) * kInstanceSize);
      r.set_evidence_offset(
          m.header.pixel_offset +
          std::uint64_t(q.pixel_index) * kPixelSize);
      out.push_back(r);
    }
    return true;
  }

 private:
  bool inject_failure_;
};

class EventLog {
 public:
  EventLog(std::string path, std::string run_id)
      : path_(std::move(path)), run_id_(std::move(run_id)) {
    write_text_atomic(path_, "[\n");
  }
  void emit(const std::string& level, const std::string& event,
            const std::string& detail_json) {
    std::ostringstream os;
    os << (first_ ? "" : ",\n") << "  {\"ts\":\"" << iso_utc_now()
       << "\",\"run_id\":\"" << json_escape(run_id_)
       << "\",\"level\":\"" << level << "\",\"event\":\"" << event
       << "\",\"detail\":{" << detail_json << "}}";
    std::ofstream f(path_, std::ios::app);
    f << os.str();
    first_ = false;
  }
  void close() { write_text_atomic_append_close(); }

 private:
  void write_text_atomic_append_close() {
    std::ofstream f(path_, std::ios::app);
    f << "\n]\n";
  }
  std::string path_;
  std::string run_id_;
  bool first_ = true;
};

std::string ckpt_path(const RunConfig& c) {
  return join_path(c.work_dir, "state/checkpoint.json");
}
std::string results_path(const RunConfig& c) {
  return join_path(c.work_dir, "evidence/results.bin");
}

bool save_checkpoint(const RunConfig& c, const Checkpoint& s) {
  std::ostringstream os;
  os << "{\n"
     << "  \"format_version\": " << s.format_version << ",\n"
     << "  \"run_id\": \"" << json_escape(s.run_id) << "\",\n"
     << "  \"created_at\": \"" << s.created_at << "\",\n"
     << "  \"next_instance\": " << s.next_instance << ",\n"
     << "  \"total_instances\": " << s.total_instances << ",\n"
     << "  \"backend\": " << s.backend << ",\n"
     << "  \"sequence\": " << s.sequence << ",\n"
     << "  \"failed_checks\": " << s.failed_checks << ",\n"
     << "  \"material_sha256\": \"" << s.material_sha256 << "\",\n"
     << "  \"started_at\": \"" << json_escape(s.started_at) << "\"\n"
     << "}\n";
  return write_text_atomic(ckpt_path(c), os.str());
}

bool load_checkpoint(const RunConfig& c, Checkpoint& s) {
  std::vector<std::uint8_t> raw;
  if (!read_file(ckpt_path(c), raw)) return false;
  std::string text(raw.begin(), raw.end());
  auto grab = [&](const std::string& key) -> std::string {
    std::string needle = "\"" + key + "\"";
    auto p = text.find(needle);
    if (p == std::string::npos) return {};
    p += needle.size();
    p = text.find(':', p);
    if (p == std::string::npos) return {};
    auto a = text.find_first_of("\"0123456789", p + 1);
    if (a == std::string::npos) return {};
    if (text[a] == '"') {
      auto e = text.find('"', a + 1);
      return text.substr(a + 1, e - a - 1);
    }
    auto e = text.find_first_of(",}\n", a);
    return text.substr(a, e - a);
  };
  s.run_id = grab("run_id");
  s.created_at = grab("created_at");
  s.material_sha256 = grab("material_sha256");
  s.next_instance =
      static_cast<std::uint32_t>(std::stoul(grab("next_instance")));
  s.total_instances =
      static_cast<std::uint32_t>(std::stoul(grab("total_instances")));
  s.backend =
      static_cast<std::uint32_t>(std::stoul(grab("backend")));
  s.sequence = std::stoull(grab("sequence"));
  s.format_version =
      static_cast<std::uint32_t>(std::stoul(grab("format_version")));
  s.failed_checks =
      static_cast<std::uint32_t>(std::stoul(grab("failed_checks")));
  s.started_at = grab("started_at");
  return true;
}

}  // namespace

std::unique_ptr<IBackend> create_backend(const BackendOptions& opt,
                                         DegradeReason& reason,
                                         std::string& detail,
                                         BackendCounters& counters) {
  reason = kDegradeNone;
  detail.clear();
  if (!opt.force_cpu) {
    if (auto* vk = VulkanBackend::create(opt, reason, detail, counters))
      return std::unique_ptr<IBackend>(vk);
    counters.degraded_chunks++;
  } else {
    reason = kDegradeForcedByFlag;
    detail = "forced CPU via options";
  }
  return std::unique_ptr<IBackend>(
      new CpuBackend(opt.force_chunk_failure));
}

namespace {

bool append_results(const std::string& path,
                    const std::vector<ResultRecord>& recs) {
  std::ofstream f(path, std::ios::binary | std::ios::app);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(recs.data()),
          static_cast<std::streamsize>(recs.size() *
                                       sizeof(ResultRecord)));
  return bool(f);
}

std::uint32_t count_result_records(const std::string& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return 0;
  auto sz = f.tellg();
  return static_cast<std::uint32_t>(sz / kResultRecordSize);
}

const char* degrade_name(std::uint32_t r) {
  switch (r) {
    case kDegradeNone: return "none";
    case kDegradeNoPhysicalDevice: return "no_physical_device";
    case kDegradeNoComputeQueue: return "no_compute_queue";
    case kDegradeAllocationFailed: return "allocation_failed";
    case kDegradePipelineFailed: return "pipeline_creation_failed";
    case kDegradeGlfwUnavailable: return "glfw_unavailable";
    case kDegradeForcedByFlag: return "forced_by_flag";
    default: return "unknown";
  }
}

}  // namespace

int run_pipeline(const RunConfig& cfg) {
  ensure_dir(join_path(cfg.work_dir, "state"));
  ensure_dir(join_path(cfg.work_dir, "evidence"));

  Material material;
  std::string err;
  if (!load_material(cfg.material_path, material, err)) {
    std::cerr << "material error: " << err << "\n";
    return kExitMaterial;
  }
  std::string material_hash = sha256_hex_file(cfg.material_path);

  EventLog events(join_path(cfg.work_dir, "evidence/event_log.json"),
                  cfg.run_id);
  events.emit("info", "run_start",
              "\"run_id\":\"" + json_escape(cfg.run_id) +
                  "\",\"material\":\"" +
                  json_escape(cfg.material_path) +
                  "\",\"material_sha256\":\"" + material_hash + "\"");

  Checkpoint ck;
  bool resuming = file_exists(ckpt_path(cfg));
  std::string rpath = results_path(cfg);
  if (resuming) {
    if (!load_checkpoint(cfg, ck)) {
      events.emit("error", "checkpoint_corrupt", "");
      events.close();
      return kExitCheckpoint;
    }
    if (ck.material_sha256 != material_hash) {
      events.emit("error", "material_changed",
                  "\"expected\":\"" + ck.material_sha256 +
                      "\",\"actual\":\"" + material_hash + "\"");
      events.close();
      return kExitCheckpoint;
    }
    std::uint32_t have = count_result_records(rpath);
    if (have != ck.next_instance) {
      events.emit("error", "results_checkpoint_mismatch",
                  "\"results_records\":" + std::to_string(have) +
                      ",\"checkpoint_next\":" +
                      std::to_string(ck.next_instance));
      events.close();
      return kExitCheckpoint;
    }
    events.emit("info", "resume",
                "\"next_instance\":" +
                    std::to_string(ck.next_instance) +
                    ",\"sequence\":" + std::to_string(ck.sequence));
  } else {
    ck.run_id = cfg.run_id;
    ck.total_instances = material.header.instance_count;
    ck.next_instance = 0;
    ck.sequence = 0;
    ck.material_sha256 = material_hash;
    ck.created_at = iso_utc_now();
    ck.started_at = ck.created_at;
    write_text_atomic(rpath, std::string{});
  }

  BackendOptions bopt;
  bopt.force_cpu = cfg.force_cpu;
  bopt.force_gpu_oom = cfg.force_gpu_oom;
  bopt.force_chunk_failure = cfg.force_chunk_failure;
  bopt.chunk_size = cfg.chunk_size;
  BackendCounters counters;
  DegradeReason reason;
  std::string detail;
  auto backend = create_backend(bopt, reason, detail, counters);
  ck.backend = backend->kind();
  RunSummary summary;
  summary.run_id = cfg.run_id;
  summary.started_at = ck.started_at.empty() ? ck.created_at
                                              : ck.started_at;
  summary.backend = ck.backend;
  DegradeReason effective_degrade = reason;
  summary.degrade_reason = reason;
  summary.total = ck.total_instances;
  summary.material_sha256 = material_hash;
  events.emit("info", "backend_selected",
              "\"backend\":" + std::to_string(ck.backend) +
                  ",\"degrade_reason\":\"" + degrade_name(reason) +
                  "\",\"detail\":\"" + json_escape(detail) + "\"");

  std::uint32_t chunk = cfg.chunk_size;
  std::uint32_t processed_this_run = 0;
  bool paused = false;

  while (ck.next_instance < ck.total_instances) {
    std::uint32_t remain = ck.total_instances - ck.next_instance;
    std::uint32_t take = std::min(chunk, remain);
    std::vector<ResultRecord> recs;
    std::string verr;
    bool ok = backend->verify_range(material, ck.next_instance, take,
                                    recs, verr);

    // Resource exhaustion: shrink chunk and retry once, then degrade to CPU.
    while (!ok && (chunk > 1 || ck.backend == kBackendVulkan)) {
      counters.allocation_failures++;
      events.emit("warn", "resource_pressure",
                  "\"chunk\":" + std::to_string(take) +
                      ",\"error\":\"" + json_escape(verr) + "\"");
      if (ck.backend == kBackendVulkan) {
        effective_degrade = kDegradeAllocationFailed;
        summary.degrade_reason = kDegradeAllocationFailed;
        summary.degrade_detail = verr;
        backend.reset(new CpuBackend(bopt.force_chunk_failure));
        ck.backend = kBackendCpu;
        summary.backend = kBackendCpu;
        counters.degraded_chunks++;
        events.emit("warn", "degrade_to_cpu",
                    "\"reason\":\"allocation_failed\"");
        ok = backend->verify_range(material, ck.next_instance, take,
                                   recs, verr);
        if (ok) break;
      }
      if (chunk > 1) {
        chunk = std::max(1u, chunk / 2);
        summary.chunk_shrink_events++;
        take = std::min(chunk, remain);
        events.emit("warn", "chunk_shrink",
                    "\"new_chunk\":" + std::to_string(chunk));
        ok = backend->verify_range(material, ck.next_instance, take,
                                   recs, verr);
      } else {
        break;
      }
    }
    if (!ok) {
      events.emit("error", "fatal_resource",
                  "\"error\":\"" + json_escape(verr) + "\"");
      events.close();
      save_report(cfg.work_dir, summary, material, effective_degrade, counters,
                  ck.next_instance, true);
      return kExitResource;
    }

    if (!append_results(rpath, recs)) {
      events.emit("error", "results_write_failed", "");
      events.close();
      return kExitResource;
    }
    ck.next_instance += static_cast<std::uint32_t>(recs.size());
    ck.sequence++;
    ck.created_at = iso_utc_now();
    processed_this_run += static_cast<std::uint32_t>(recs.size());
    summary.chunks++;
    std::uint32_t chunk_fails = 0;
    for (const auto& r : recs)
      if (r.fail_mask) chunk_fails++;
    ck.failed_checks += chunk_fails;
    summary.failed_checks = ck.failed_checks;
    if (!save_checkpoint(cfg, ck)) {
      events.emit("error", "checkpoint_write_failed", "");
      events.close();
      return kExitCheckpoint;
    }
    events.emit("info", "chunk_committed",
                "\"sequence\":" + std::to_string(ck.sequence) +
                    ",\"next_instance\":" +
                    std::to_string(ck.next_instance));

    if (cfg.pause_after != UINT32_MAX &&
        processed_this_run >= cfg.pause_after &&
        ck.next_instance < ck.total_instances) {
      paused = true;
      events.emit("info", "paused",
                  "\"next_instance\":" +
                      std::to_string(ck.next_instance));
      break;
    }
  }

  summary.processed = ck.next_instance;
  summary.paused = paused;
  summary.ended_at = iso_utc_now();
  summary.counters = counters;
  summary.results_sha256 = sha256_hex_file(rpath);
  events.emit("info", paused ? "run_suspended" : "run_complete",
              "\"processed\":" + std::to_string(summary.processed) +
                  ",\"failed_checks\":" +
                  std::to_string(summary.failed_checks));
  events.close();
  save_report(cfg.work_dir, summary, material, effective_degrade, counters,
              ck.next_instance, false);
  return paused ? kExitPaused : kExitOk;
}

}  // namespace nmts
