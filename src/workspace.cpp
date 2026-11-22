#include "workspace.h"
#include "cloud.h"
#include "sha256.h"
#include "util.h"

#include <sstream>

namespace pcd {

static const char* kDefaultParams = R"PARAMS(# Extreme parameter sets for density clipping.
# Format: set <name> radius=<r> min_neighbors=<n> max_neighbors=<n>
# radius=0 or min/max at type limits are deliberate extreme values.
set extreme-tight   radius=0.5   min_neighbors=4  max_neighbors=64
set extreme-loose   radius=8.0   min_neighbors=1  max_neighbors=4096
set extreme-minonly radius=2.0   min_neighbors=12 max_neighbors=4294967295
set extreme-maxonly radius=2.0   min_neighbors=0  max_neighbors=8
)PARAMS";

void ws_init(const Workspace& ws, const std::string& source_cloud) {
  if (file_exists(ws.manifest_path()))
    die("workspace already initialized: " + ws.root);
  if (!file_exists(source_cloud))
    die("source cloud not found: " + source_cloud);

  make_dirs(ws.root);
  make_dirs(ws.root + "/original");
  make_dirs(ws.runs_dir());

  // Preserve an unmodified copy of the original material, then lock it.
  copy_file(source_cloud, ws.original_bin());
  std::string hash = Sha256::file_hex(ws.original_bin());
  write_text(ws.original_sha(), hash + "  original/cloud.bin\n");
  make_readonly(ws.original_bin());
  make_readonly(ws.original_sha());

  Cloud c = load_cloud(ws.original_bin());

  if (!file_exists(ws.params_path()))
    write_text(ws.params_path(), kDefaultParams);

  std::ostringstream m;
  m << "{\n"
    << "  \"tool\": \"pcdclip\",\n"
    << "  \"version\": 1,\n"
    << "  \"created_utc\": \"" << utc_now_iso() << "\",\n"
    << "  \"source_path\": \"" << json_escape(source_cloud) << "\",\n"
    << "  \"original_sha256\": \"" << hash << "\",\n"
    << "  \"original_points\": " << c.original_count << ",\n"
    << "  \"point_stride_bytes\": " << kPointStride << "\n"
    << "}\n";
  write_text(ws.manifest_path(), m.str());
}

bool ws_original_intact(const Workspace& ws, std::string* actual_hash) {
  std::string recorded = json_get(
      std::string(reinterpret_cast<const char*>(
                      read_file(ws.manifest_path()).data()),
                  file_size(ws.manifest_path())),
      "original_sha256");
  std::string actual = Sha256::file_hex(ws.original_bin());
  if (actual_hash) *actual_hash = actual;
  return actual == recorded;
}

std::string json_get(const std::string& json, const std::string& key) {
  std::string needle = "\"" + key + "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return "";
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return "";
  ++pos;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
  if (pos >= json.size()) return "";
  if (json[pos] == '"') {
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
  }
  auto end = json.find_first_of(",}\n", pos);
  return json.substr(pos, end - pos);
}

} // namespace pcd
