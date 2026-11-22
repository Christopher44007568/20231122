#include "compare.h"
#include "cloud.h"
#include "util.h"

#include <cstdio>
#include <sstream>
#include <unordered_set>

namespace pcd {

static std::unordered_set<uint64_t> clipped_set(const Cloud& c) {
  std::unordered_set<uint64_t> s;
  for (uint64_t i = 0; i < c.points.size(); ++i)
    if (c.points[i].flags & kFlagClipped) s.insert(i);
  return s;
}

void compare_runs(const Workspace& ws, const std::string& a,
                  const std::string& b) {
  std::string pa = (a == "original") ? ws.original_bin() : ws.run_output(a);
  std::string pb = (b == "original") ? ws.original_bin() : ws.run_output(b);
  if (!file_exists(pa)) die("no output for: " + a);
  if (!file_exists(pb)) die("no output for: " + b);
  Cloud ca = load_cloud(pa), cb = load_cloud(pb);
  if (ca.points.size() != cb.points.size())
    die("point count mismatch; both sides must share the original "
        "index space");
  auto sa = clipped_set(ca), sb = clipped_set(cb);

  uint64_t both = 0;
  std::vector<uint64_t> only_a, only_b;
  for (uint64_t i : sa) {
    if (sb.count(i)) ++both;
    else only_a.push_back(i);
  }
  for (uint64_t i : sb)
    if (!sa.count(i)) only_b.push_back(i);

  std::string stamp = utc_now_compact();
  std::string dir = (a == "original") ? ws.root + "/compare"
                                      : ws.run_evidence(a);
  make_dirs(dir);
  std::string path = dir + "/compare_" + a + "_vs_" + b + "_" + stamp +
                     ".json";
  auto dump = [](const std::vector<uint64_t>& v, size_t cap) {
    std::ostringstream s;
    s << "[";
    for (size_t i = 0; i < v.size() && i < cap; ++i) {
      if (i) s << ", ";
      s << v[i];
    }
    s << "]";
    return s.str();
  };
  std::ostringstream j;
  j << "{\n"
    << "  \"a\": \"" << json_escape(a) << "\",\n"
    << "  \"b\": \"" << json_escape(b) << "\",\n"
    << "  \"compared_utc\": \"" << utc_now_iso() << "\",\n"
    << "  \"points\": " << ca.points.size() << ",\n"
    << "  \"clipped_a\": " << sa.size() << ",\n"
    << "  \"clipped_b\": " << sb.size() << ",\n"
    << "  \"clipped_both\": " << both << ",\n"
    << "  \"only_a_count\": " << only_a.size() << ",\n"
    << "  \"only_b_count\": " << only_b.size() << ",\n"
    << "  \"only_a_offsets_first50\": " << dump(only_a, 50) << ",\n"
    << "  \"only_b_offsets_first50\": " << dump(only_b, 50) << ",\n"
    << "  \"note\": \"offsets are point indices; byte offset = index * "
    << kPointStride << "\"\n"
    << "}\n";
  write_text(path, j.str());
  std::printf("compare %s vs %s: clipped a=%zu b=%zu both=%llu "
              "only_a=%zu only_b=%zu\n  report: %s\n",
              a.c_str(), b.c_str(), sa.size(), sb.size(),
              (unsigned long long)both, only_a.size(), only_b.size(),
              path.c_str());
}

} // namespace pcd
