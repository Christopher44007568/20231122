#include "cloud.h"
#include "compare.h"
#include "params.h"
#include "report.h"
#include "runner.h"
#include "util.h"
#include "viewer.h"
#include "vk.h"
#include "workspace.h"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>

using namespace pcd;

static void on_signal(int) { g_stop = 1; }

static void usage() {
  std::printf(
      "pcdclip - point-cloud density clipping, local correction tool\n"
      "\n"
      "  gen <out.bin> <n> [seed]     generate a synthetic cloud\n"
      "  init <ws> <cloud.bin>        create workspace, preserve the\n"
      "                               unmodified original material\n"
      "  plan <ws>                    duplicate one run object per\n"
      "                               extreme parameter set\n"
      "  list <ws>                    list run objects and states\n"
      "  run <ws> <id|all> [--chunk N]  execute/resume a run\n"
      "  compare <ws> <a> <b>         diff two runs (or 'original')\n"
      "  export <ws> <id> <out_dir>   export corrected cloud+evidence\n"
      "  accept <ws> <id>             acceptance check (timestamps +\n"
      "                               original offsets)\n"
      "  view <ws> <id|original>      interactive GLFW viewer\n"
      "  info                         probe the Vulkan device\n");
}

static int cmd_list(const Workspace& ws) {
  DIR* d = opendir(ws.runs_dir().c_str());
  if (!d) die("no runs; run 'plan' first");
  while (auto* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    std::string id = e->d_name;
    if (!file_exists(ws.run_json(id))) continue;
    auto b = read_file(ws.run_json(id));
    std::string j(reinterpret_cast<const char*>(b.data()), b.size());
    std::printf("%-16s state=%-12s attempts=%s radius=%s min=%s "
                "max=%s\n", id.c_str(), json_get(j, "state").c_str(),
                json_get(j, "attempts").c_str(),
                json_get(j, "radius").c_str(),
                json_get(j, "min_neighbors").c_str(),
                json_get(j, "max_neighbors").c_str());
  }
  closedir(d);
  return 0;
}

int main(int argc, char** argv) {
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  try {
    if (argc < 2) {
      usage();
      return 1;
    }
    std::string cmd = argv[1];
    if (cmd == "gen" && argc >= 4) {
      uint64_t n = std::stoull(argv[3]);
      uint32_t seed = argc >= 5 ? std::stoul(argv[4]) : 42;
      Cloud c = generate_cloud(n, seed);
      save_cloud(argv[2], c);
      std::printf("generated %llu points -> %s\n",
                  (unsigned long long)c.points.size(), argv[2]);
      return 0;
    }
    if (cmd == "info") {
      VkContext ctx = vk_create(false);
      std::printf("vulkan device: %s (api %u.%u.%u)\n",
                  ctx.device_name.c_str(),
                  VK_VERSION_MAJOR(ctx.props.apiVersion),
                  VK_VERSION_MINOR(ctx.props.apiVersion),
                  VK_VERSION_PATCH(ctx.props.apiVersion));
      vk_destroy(ctx);
      return 0;
    }
    if (cmd == "init" && argc >= 4) {
      Workspace ws{argv[2]};
      ws_init(ws, argv[3]);
      std::printf("workspace ready: %s (original preserved read-only "
                  "under original/)\n", ws.root.c_str());
      return 0;
    }
    if (cmd == "plan" && argc >= 3) {
      ws_plan(Workspace{argv[2]});
      return 0;
    }
    if (cmd == "list" && argc >= 3) {
      return cmd_list(Workspace{argv[2]});
    }
    if (cmd == "run" && argc >= 4) {
      Workspace ws{argv[2]};
      uint64_t chunk = 65536;
      for (int i = 4; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--chunk") == 0)
          chunk = std::stoull(argv[i + 1]);
      std::string id = argv[3];
      if (id == "all") {
        int worst = 0;
        for (const auto& ps : load_param_sets(ws.params_path())) {
          int rc = run_execute(ws, ps.name, chunk);
          if (rc != 0) worst = rc;
          if (g_stop) break;
        }
        return worst;
      }
      return run_execute(ws, id, chunk);
    }
    if (cmd == "compare" && argc >= 5) {
      compare_runs(Workspace{argv[2]}, argv[3], argv[4]);
      return 0;
    }
    if (cmd == "export" && argc >= 5) {
      run_export(Workspace{argv[2]}, argv[3], argv[4]);
      return 0;
    }
    if (cmd == "accept" && argc >= 4) {
      return accept_run(Workspace{argv[2]}, argv[3]);
    }
    if (cmd == "view" && argc >= 4) {
      view_run(Workspace{argv[2]}, argv[3]);
      return 0;
    }
    usage();
    return 1;
  } catch (const std::exception& e) {
    return 1;
  }
}
