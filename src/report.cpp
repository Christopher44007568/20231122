#include "report.hpp"

#include <cstring>
#include <sstream>

#include "util.hpp"

namespace nmts {
namespace {

const char* backend_name(std::uint32_t b) {
  return b == kBackendVulkan ? "vulkan_compute" : "cpu_reference";
}

const char* dname(std::uint32_t r) {
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

void save_report(const std::string& work_dir,
                 const RunSummary& s,
                 const Material& material,
                 DegradeReason degrade,
                 const BackendCounters& counters,
                 std::uint32_t committed,
                 bool fatal) {
  std::ostringstream os;
  os << "{\n"
     << "  \"run_id\": \"" << json_escape(s.run_id) << "\",\n"
     << "  \"generated_at\": \"" << iso_utc_now() << "\",\n"
     << "  \"started_at\": \"" << json_escape(s.started_at) << "\",\n"
     << "  \"ended_at\": \"" << json_escape(s.ended_at) << "\",\n"
     << "  \"status\": \""
     << (fatal ? "fatal_resource" : (s.paused ? "paused" : "complete"))
     << "\",\n"
     << "  \"backend\": \"" << backend_name(s.backend) << "\",\n"
     << "  \"backend_code\": " << s.backend << ",\n"
     << "  \"degrade\": {\n"
     << "    \"reason\": \"" << dname(degrade) << "\",\n"
     << "    \"code\": " << degrade << ",\n"
     << "    \"detail\": \"" << json_escape(s.degrade_detail) << "\"\n"
     << "  },\n"
     << "  \"resource_counters\": {\n"
     << "    \"allocation_attempts\": "
     << counters.allocation_attempts << ",\n"
     << "    \"allocation_failures\": "
     << counters.allocation_failures << ",\n"
     << "    \"degraded_chunks\": " << counters.degraded_chunks << "\n"
     << "  },\n"
     << "  \"chunk_shrink_events\": " << s.chunk_shrink_events << ",\n"
     << "  \"chunks_committed\": " << s.chunks << ",\n"
     << "  \"instances\": {\n"
     << "    \"total\": " << s.total << ",\n"
     << "    \"committed\": " << committed << ",\n"
     << "    \"failed_check_records\": " << s.failed_checks << "\n"
     << "  },\n"
     << "  \"material\": {\n"
     << "    \"path\": \"" << json_escape(material.source_path) << "\",\n"
     << "    \"sha256\": \"" << s.material_sha256 << "\",\n"
     << "    \"width\": " << material.header.width << ",\n"
     << "    \"height\": " << material.header.height << ",\n"
     << "    \"pixel_offset\": " << material.header.pixel_offset << ",\n"
     << "    \"instance_offset\": "
     << material.header.instance_offset << "\n"
     << "  },\n"
     << "  \"results_sha256\": \"" << s.results_sha256 << "\"\n"
     << "}\n";
  write_text_atomic(join_path(work_dir, "evidence/run_report.json"),
                    os.str());
}

CompareResult compare_result_files(const std::string& a,
                                   const std::string& b,
                                   bool ignore_backend) {
  CompareResult cr;
  std::vector<std::uint8_t> ba, bb;
  if (!read_file(a, ba) || !read_file(b, bb)) {
    cr.first_difference = "cannot read one of the result files";
    return cr;
  }
  cr.records_a = ba.size() / kResultRecordSize;
  cr.records_b = bb.size() / kResultRecordSize;
  std::size_t common =
      std::min(cr.records_a, cr.records_b);
  for (std::size_t i = 0; i < common; ++i) {
    const auto* ra =
        reinterpret_cast<const ResultRecord*>(ba.data() +
                                              i * kResultRecordSize);
    const auto* rb =
        reinterpret_cast<const ResultRecord*>(bb.data() +
                                              i * kResultRecordSize);
    bool same = std::memcmp(ra, rb, kResultRecordSize) == 0;
    if (!same && ignore_backend) {
      ResultRecord ca = *ra;
      ResultRecord cb = *rb;
      ca.backend = cb.backend = 0;
      same = std::memcmp(&ca, &cb, kResultRecordSize) == 0;
    }
    if (!same) {
      cr.differing++;
      if (cr.first_difference.empty()) {
        cr.first_difference =
            "record " + std::to_string(i) +
            " instance_offset=" +
            std::to_string(ra->instance_offset()) +
            " evidence_offset=" +
            std::to_string(ra->evidence_offset()) +
            " mask_a=" + std::to_string(ra->fail_mask) +
            " mask_b=" + std::to_string(rb->fail_mask);
      }
    }
  }
  cr.identical =
      cr.differing == 0 && cr.records_a == cr.records_b &&
      ba.size() == bb.size();
  return cr;
}

}  // namespace nmts
