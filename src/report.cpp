#include "report.h"
#include "cloud.h"
#include "runner.h"
#include "sha256.h"
#include "util.h"

#include <cstdio>
#include <dirent.h>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace pcd {

struct Check {
  std::string name;
  bool ok;
  std::string detail;
};

int accept_run(const Workspace& ws, const std::string& run_id) {
  std::vector<Check> checks;
  auto add = [&](std::string name, bool ok, std::string detail) {
    checks.push_back({std::move(name), ok, std::move(detail)});
  };

  // 1. The original material must be untouched.
  std::string orig_hash;
  bool intact = ws_original_intact(ws, &orig_hash);
  add("original_intact", intact, "sha256=" + orig_hash);

  if (!file_exists(ws.run_json(run_id)))
    die("run object not found: " + run_id);
  auto rj_bytes = read_file(ws.run_json(run_id));
  std::string rj(reinterpret_cast<const char*>(rj_bytes.data()),
                 rj_bytes.size());
  std::string state = json_get(rj, "state");
  add("run_completed", state == "done", "state=" + state);

  // 2. The duplicated working copy must still equal the original.
  std::string working_hash = Sha256::file_hex(ws.run_working(run_id));
  add("working_copy_pristine", working_hash == orig_hash,
      "sha256=" + working_hash);

  // 3. Journal offsets must land inside the original material.
  uint64_t orig_points = 0;
  {
    auto mf = read_file(ws.manifest_path());
    std::string m(reinterpret_cast<const char*>(mf.data()), mf.size());
    orig_points = std::stoull(json_get(m, "original_points"));
  }
  std::vector<EditRec> edits;
  if (file_exists(ws.run_journal(run_id))) {
    uint64_t jbytes = file_size(ws.run_journal(run_id));
    bool aligned = (jbytes % sizeof(EditRec)) == 0;
    add("journal_aligned", aligned,
        "bytes=" + std::to_string(jbytes));
    edits.resize(static_cast<size_t>(jbytes / sizeof(EditRec)));
    FILE* f = std::fopen(ws.run_journal(run_id).c_str(), "rb");
    if (!edits.empty())
      std::fread(edits.data(), sizeof(EditRec), edits.size(), f);
    std::fclose(f);
  }
  uint64_t bad_offsets = 0;
  std::unordered_set<uint64_t> seen;
  uint64_t dup = 0;
  for (const auto& e : edits) {
    if (e.index >= orig_points) ++bad_offsets;
    if (!seen.insert(e.index).second) ++dup;
  }
  add("offsets_within_original", bad_offsets == 0,
      "edits=" + std::to_string(edits.size()) +
      " out_of_range=" + std::to_string(bad_offsets));
  add("offsets_unique", dup == 0, "duplicates=" + std::to_string(dup));

  // 4. Output flags must reproduce the journal exactly.
  if (file_exists(ws.run_output(run_id))) {
    Cloud out = load_cloud(ws.run_output(run_id));
    uint64_t mismatch = 0;
    for (uint64_t i = 0; i < out.points.size(); ++i) {
      bool flagged = (out.points[i].flags & kFlagClipped) != 0;
      bool journaled = seen.count(i) != 0;
      if (flagged != journaled) ++mismatch;
    }
    add("output_matches_journal", mismatch == 0,
        "mismatched_points=" + std::to_string(mismatch));
    std::string recorded = json_get(rj, "output_sha256");
    std::string actual = Sha256::file_hex(ws.run_output(run_id));
    add("output_hash_recorded", !recorded.empty() && actual == recorded,
        "sha256=" + actual);
  } else {
    add("output_matches_journal", false, "output.bin missing");
    add("output_hash_recorded", false, "output.bin missing");
  }

  // 5. Timeline: attempt start/finish timestamps must exist and be
  // ordered (ISO-8601 UTC strings compare lexicographically).
  std::string started, finished, attempt_dir;
  {
    std::string att_root = ws.run_evidence(run_id) + "/attempts";
    DIR* d = ::opendir(att_root.c_str());
    std::string latest;
    if (d) {
      while (auto* e = ::readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        if (name > latest) latest = name; // UTC stamps sort by time
      }
      ::closedir(d);
    }
    if (!latest.empty()) attempt_dir = att_root + "/" + latest;
  }
  if (!attempt_dir.empty() &&
      file_exists(attempt_dir + "/manifest.json")) {
    auto mb = read_file(attempt_dir + "/manifest.json");
    std::string m(reinterpret_cast<const char*>(mb.data()), mb.size());
    started = json_get(m, "started_utc");
    finished = json_get(m, "finished_utc");
  }
  add("timeline_recorded",
      !started.empty() && !finished.empty() && started <= finished,
      "started=" + started + " finished=" + finished);

  // Verdict + report anchored to concrete timestamps and offsets.
  bool pass = true;
  for (const auto& c : checks) pass = pass && c.ok;
  std::ostringstream j;
  j << "{\n"
    << "  \"run_id\": \"" << json_escape(run_id) << "\",\n"
    << "  \"verdict\": \"" << (pass ? "PASS" : "FAIL") << "\",\n"
    << "  \"generated_utc\": \"" << utc_now_iso() << "\",\n"
    << "  \"original_sha256\": \"" << orig_hash << "\",\n"
    << "  \"attempt_started_utc\": \"" << started << "\",\n"
    << "  \"attempt_finished_utc\": \"" << finished << "\",\n"
    << "  \"clipped_total\": " << edits.size() << ",\n"
    << "  \"clipped_offsets_first20\": [";
  for (size_t i = 0; i < edits.size() && i < 20; ++i) {
    if (i) j << ", ";
    j << "{\"index\": " << edits[i].index
      << ", \"byte_offset\": " << edits[i].index * kPointStride << "}";
  }
  j << "],\n  \"checks\": [\n";
  for (size_t i = 0; i < checks.size(); ++i) {
    j << "    {\"name\": \"" << checks[i].name << "\", \"ok\": "
      << (checks[i].ok ? "true" : "false") << ", \"detail\": \""
      << json_escape(checks[i].detail) << "\"}"
      << (i + 1 < checks.size() ? ",\n" : "\n");
  }
  j << "  ]\n}\n";
  std::string out_path = ws.run_evidence(run_id) +
                         "/acceptance_report.json";
  make_dirs(ws.run_evidence(run_id));
  write_text(out_path, j.str());

  std::printf("acceptance %s: %s\n", run_id.c_str(),
              pass ? "PASS" : "FAIL");
  for (const auto& c : checks)
    std::printf("  [%s] %s: %s\n", c.ok ? "ok" : "!!", c.name.c_str(),
                c.detail.c_str());
  std::printf("  window: %s .. %s\n  report: %s\n", started.c_str(),
              finished.c_str(), out_path.c_str());
  return pass ? 0 : 1;
}

} // namespace pcd
