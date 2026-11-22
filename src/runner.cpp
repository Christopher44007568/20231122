#include "runner.h"
#include "cloud.h"
#include "render.h"
#include "sha256.h"
#include "util.h"
#include "vk.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <sstream>
#include <unistd.h>

namespace pcd {

volatile sig_atomic_t g_stop = 0;

// ---------- plan: duplicate one run object per extreme parameter set ----------

void ws_plan(const Workspace& ws) {
  if (!file_exists(ws.manifest_path()))
    die("workspace not initialized: " + ws.root);
  auto sets = load_param_sets(ws.params_path());
  std::string orig_hash = Sha256::file_hex(ws.original_bin());
  for (const auto& ps : sets) {
    std::string dir = ws.run_dir(ps.name);
    make_dirs(dir);
    // The duplicated object: an independent working copy of the
    // original material. The pristine copy under original/ is never
    // touched by runs.
    if (!file_exists(ws.run_working(ps.name))) {
      copy_file(ws.original_bin(), ws.run_working(ps.name));
    }
    if (!file_exists(ws.run_json(ps.name))) {
      std::ostringstream j;
      j << "{\n"
        << "  \"run_id\": \"" << json_escape(ps.name) << "\",\n"
        << "  \"created_utc\": \"" << utc_now_iso() << "\",\n"
        << "  \"radius\": " << ps.radius << ",\n"
        << "  \"min_neighbors\": " << ps.min_neighbors << ",\n"
        << "  \"max_neighbors\": " << ps.max_neighbors << ",\n"
        << "  \"source_sha256\": \"" << orig_hash << "\",\n"
        << "  \"state\": \"planned\",\n"
        << "  \"attempts\": 0\n"
        << "}\n";
      write_text(ws.run_json(ps.name), j.str());
    }
    std::printf("planned run object: %s\n", ps.name.c_str());
  }
}

// ---------- CPU uniform grid (uploaded to the GPU as-is) ----------

struct Grid {
  int32_t dims[3];
  float origin[3];
  float cell;
  std::vector<uint32_t> order;  // point indices sorted by cell
  std::vector<uint32_t> start;  // per-cell begin in order
  std::vector<uint32_t> count;  // per-cell count
};

static Grid build_grid(const Cloud& c, float radius) {
  float lo[3], hi[3];
  cloud_bounds(c, lo, hi);
  Grid g;
  const int32_t kMaxDim = 512;
  float extent = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
  if (extent <= 0.0f) extent = 1.0f;
  // cell >= radius keeps the 27-cell neighborhood search correct.
  g.cell = std::max(radius, extent / static_cast<float>(kMaxDim));
  for (int a = 0; a < 3; ++a) {
    float e = hi[a] - lo[a];
    g.dims[a] = std::max(1, std::min(kMaxDim,
        static_cast<int32_t>(std::ceil(e / g.cell)) + 1));
    g.origin[a] = lo[a];
  }
  uint64_t cells = static_cast<uint64_t>(g.dims[0]) * g.dims[1] * g.dims[2];
  g.start.assign(static_cast<size_t>(cells), 0);
  g.count.assign(static_cast<size_t>(cells), 0);
  auto cell_of = [&](const Point& p) -> uint64_t {
    int32_t c[3] = {
      static_cast<int32_t>((p.x - g.origin[0]) / g.cell),
      static_cast<int32_t>((p.y - g.origin[1]) / g.cell),
      static_cast<int32_t>((p.z - g.origin[2]) / g.cell)};
    for (int a = 0; a < 3; ++a)
      c[a] = std::max(0, std::min(g.dims[a] - 1, c[a]));
    return static_cast<uint64_t>(c[0]) +
           static_cast<uint64_t>(g.dims[0]) *
               (c[1] + static_cast<uint64_t>(g.dims[1]) * c[2]);
  };
  for (const auto& p : c.points) g.count[cell_of(p)]++;
  uint32_t acc = 0;
  for (size_t i = 0; i < g.count.size(); ++i) {
    g.start[i] = acc;
    acc += g.count[i];
  }
  g.order.resize(c.points.size());
  std::vector<uint32_t> cursor = g.start;
  for (uint32_t i = 0; i < c.points.size(); ++i)
    g.order[cursor[cell_of(c.points[i])]++] = i;
  return g;
}

// ---------- checkpoint ----------

struct Checkpoint {
  uint64_t next_chunk = 0;
  uint64_t dropped = 0;
  bool valid = false;
};

static Checkpoint read_checkpoint(const std::string& path) {
  Checkpoint cp;
  if (!file_exists(path)) return cp;
  auto bytes = read_file(path);
  std::string j(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  std::string nc = json_get(j, "next_chunk");
  std::string dr = json_get(j, "dropped_so_far");
  if (nc.empty() || dr.empty()) return cp;
  cp.next_chunk = std::stoull(nc);
  cp.dropped = std::stoull(dr);
  cp.valid = true;
  return cp;
}

static void write_checkpoint(const std::string& path, uint64_t next_chunk,
                             uint64_t chunks_total, uint64_t dropped,
                             const std::string& attempt) {
  std::ostringstream j;
  j << "{\n"
    << "  \"attempt\": \"" << attempt << "\",\n"
    << "  \"next_chunk\": " << next_chunk << ",\n"
    << "  \"chunks_total\": " << chunks_total << ",\n"
    << "  \"dropped_so_far\": " << dropped << ",\n"
    << "  \"updated_utc\": \"" << utc_now_iso() << "\"\n"
    << "}\n";
  std::string tmp = path + ".tmp";
  write_text(tmp, j.str());
  if (std::rename(tmp.c_str(), path.c_str()) != 0)
    die("checkpoint rename failed");
}

// ---------- GPU compute pipeline ----------

struct ComputePipe {
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorPool pool = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
};

static ComputePipe make_compute_pipe(VkContext& ctx) {
  ComputePipe cp;
  VkDescriptorSetLayoutBinding binds[4]{};
  for (int i = 0; i < 4; ++i) {
    binds[i].binding = static_cast<uint32_t>(i);
    binds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[i].descriptorCount = 1;
    binds[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo li{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  li.bindingCount = 4;
  li.pBindings = binds;
  VK_CHECK(vkCreateDescriptorSetLayout(ctx.device, &li, nullptr,
                                       &cp.set_layout));

  VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 64};
  VkPipelineLayoutCreateInfo pli{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pli.setLayoutCount = 1;
  pli.pSetLayouts = &cp.set_layout;
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges = &pcr;
  VK_CHECK(vkCreatePipelineLayout(ctx.device, &pli, nullptr,
                                  &cp.pipe_layout));

  std::string spv = std::string(PCDCLIP_SHADER_DIR) + "/density.comp.spv";
  VkShaderModule mod = vk_shader(ctx, spv);
  VkComputePipelineCreateInfo ci{
      VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  ci.stage.module = mod;
  ci.stage.pName = "main";
  ci.layout = cp.pipe_layout;
  VK_CHECK(vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &ci,
                                    nullptr, &cp.pipeline));
  vkDestroyShaderModule(ctx.device, mod, nullptr);

  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
  VkDescriptorPoolCreateInfo pi{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pi.maxSets = 1;
  pi.poolSizeCount = 1;
  pi.pPoolSizes = &ps;
  VK_CHECK(vkCreateDescriptorPool(ctx.device, &pi, nullptr, &cp.pool));

  VkDescriptorSetAllocateInfo ai{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = cp.pool;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &cp.set_layout;
  VK_CHECK(vkAllocateDescriptorSets(ctx.device, &ai, &cp.set));
  return cp;
}

static void destroy_compute_pipe(VkContext& ctx, ComputePipe& cp) {
  vkDestroyDescriptorPool(ctx.device, cp.pool, nullptr);
  vkDestroyPipeline(ctx.device, cp.pipeline, nullptr);
  vkDestroyPipelineLayout(ctx.device, cp.pipe_layout, nullptr);
  vkDestroyDescriptorSetLayout(ctx.device, cp.set_layout, nullptr);
  cp = ComputePipe{};
}

// ---------- run execution with checkpoint/resume ----------

struct PushConsts {
  float origin_cell[4];
  int32_t dims_base[4];
  int32_t counts[4];
  float params[4];
};
static_assert(sizeof(PushConsts) == 64, "push constant layout");

static void update_run_json(const Workspace& ws, const std::string& id,
                            const std::string& state, uint64_t attempts,
                            const std::string& extra_key,
                            const std::string& extra_val) {
  auto bytes = read_file(ws.run_json(id));
  std::string j(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  ParamSet ps;
  ps.name = id;
  ps.radius = std::stof(json_get(j, "radius"));
  ps.min_neighbors = std::stoul(json_get(j, "min_neighbors"));
  ps.max_neighbors = std::stoul(json_get(j, "max_neighbors"));
  std::ostringstream o;
  o << "{\n"
    << "  \"run_id\": \"" << json_escape(id) << "\",\n"
    << "  \"created_utc\": \"" << json_get(j, "created_utc") << "\",\n"
    << "  \"radius\": " << ps.radius << ",\n"
    << "  \"min_neighbors\": " << ps.min_neighbors << ",\n"
    << "  \"max_neighbors\": " << ps.max_neighbors << ",\n"
    << "  \"source_sha256\": \"" << json_get(j, "source_sha256") << "\",\n"
    << "  \"state\": \"" << state << "\",\n"
    << "  \"attempts\": " << attempts << ",\n"
    << "  \"updated_utc\": \"" << utc_now_iso() << "\"";
  if (!extra_key.empty())
    o << ",\n  \"" << extra_key << "\": \"" << extra_val << "\"";
  o << "\n}\n";
  write_text(ws.run_json(id), o.str());
}

int run_execute(const Workspace& ws, const std::string& run_id,
                uint64_t chunk_size) {
  if (!file_exists(ws.run_json(run_id)))
    die("run object not found (run 'plan' first): " + run_id);
  auto rj_bytes = read_file(ws.run_json(run_id));
  std::string rj(reinterpret_cast<const char*>(rj_bytes.data()),
                 rj_bytes.size());
  ParamSet ps;
  ps.name = run_id;
  ps.radius = std::stof(json_get(rj, "radius"));
  ps.min_neighbors = std::stoul(json_get(rj, "min_neighbors"));
  ps.max_neighbors = std::stoul(json_get(rj, "max_neighbors"));
  uint64_t attempts = std::stoull(json_get(rj, "attempts")) + 1;

  // Evidence: the duplicated working copy must still match the
  // pristine original before we touch anything.
  std::string working_hash = Sha256::file_hex(ws.run_working(run_id));
  std::string orig_hash = json_get(rj, "source_sha256");
  if (working_hash != orig_hash)
    die("working copy diverged from original material: " + run_id);

  std::string attempt = utc_now_compact();
  std::string ev = ws.run_evidence(run_id) + "/attempts/" + attempt;
  make_dirs(ev);
  std::string log_path = ev + "/log.txt";
  auto log = [&](const std::string& msg) {
    append_text(log_path, utc_now_iso() + " " + msg + "\n");
  };
  std::string started_utc = utc_now_iso();
  log("attempt " + attempt + " started, run=" + run_id);

  Cloud cloud = load_cloud(ws.run_working(run_id));
  uint64_t n = cloud.points.size();
  if (chunk_size == 0) chunk_size = 65536;
  uint64_t chunks_total = (n + chunk_size - 1) / chunk_size;
  Grid grid = build_grid(cloud, ps.radius);

  Checkpoint cp = read_checkpoint(ws.run_checkpoint(run_id));
  if (cp.valid && cp.next_chunk >= chunks_total) {
    // Previous attempt completed: this is an individual re-run of the
    // object, so start from scratch (prior evidence stays on disk).
    log("previous attempt complete; starting fresh re-run");
    cp = Checkpoint{};
    ::unlink(ws.run_checkpoint(run_id).c_str());
    ::unlink(ws.run_journal(run_id).c_str());
  }
  if (cp.valid && cp.next_chunk > 0) {
    log("resuming at chunk " + std::to_string(cp.next_chunk) + "/" +
        std::to_string(chunks_total));
    // Drop any torn tail from a crash between journal append and
    // checkpoint write.
    ::truncate(ws.run_journal(run_id).c_str(),
               static_cast<off_t>(cp.dropped * sizeof(EditRec)));
  }

  VkContext ctx = vk_create(false);
  log("vulkan device: " + ctx.device_name);
  ComputePipe pipe = make_compute_pipe(ctx);

  VkDeviceSize pts_bytes = n * sizeof(Point);
  VkBuffer2 pts = vk_buffer(ctx, pts_bytes,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
  vk_upload(ctx, pts, cloud.points.data(), pts_bytes);
  VkBuffer2 order = vk_buffer(ctx, grid.order.size() * 4,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
  vk_upload(ctx, order, grid.order.data(), grid.order.size() * 4);
  VkBuffer2 cstart = vk_buffer(ctx, grid.start.size() * 4,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
  vk_upload(ctx, cstart, grid.start.data(), grid.start.size() * 4);
  VkBuffer2 ccount = vk_buffer(ctx, grid.count.size() * 4,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
  vk_upload(ctx, ccount, grid.count.data(), grid.count.size() * 4);

  VkDescriptorBufferInfo infos[4] = {
      {pts.buf, 0, pts_bytes},
      {order.buf, 0, grid.order.size() * 4},
      {cstart.buf, 0, grid.start.size() * 4},
      {ccount.buf, 0, grid.count.size() * 4}};
  VkWriteDescriptorSet writes[4]{};
  for (int i = 0; i < 4; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = pipe.set;
    writes[i].dstBinding = static_cast<uint32_t>(i);
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].pBufferInfo = &infos[i];
  }
  vkUpdateDescriptorSets(ctx.device, 4, writes, 0, nullptr);

  // Chunked dispatch: each chunk is a checkpoint boundary, so a
  // mid-way exit (SIGINT/SIGTERM) can be re-entered losslessly.
  FILE* journal = std::fopen(ws.run_journal(run_id).c_str(), "ab");
  if (!journal) die("cannot open journal");
  uint64_t dropped = cp.valid ? cp.dropped : 0;
  bool interrupted = false;
  for (uint64_t chunk = cp.valid ? cp.next_chunk : 0; chunk < chunks_total;
       ++chunk) {
    if (g_stop) {
      interrupted = true;
      break;
    }
    uint64_t base = chunk * chunk_size;
    uint64_t count = std::min(chunk_size, n - base);

    PushConsts pc{};
    for (int a = 0; a < 3; ++a) {
      pc.origin_cell[a] = grid.origin[a];
      pc.dims_base[a] = grid.dims[a];
    }
    pc.origin_cell[3] = grid.cell;
    pc.dims_base[3] = static_cast<int32_t>(base);
    pc.counts[0] = static_cast<int32_t>(count);
    pc.counts[1] = static_cast<int32_t>(ps.min_neighbors);
    pc.counts[2] = static_cast<int32_t>(ps.max_neighbors);
    pc.params[0] = ps.radius * ps.radius;

    vk_one_shot(ctx, [&](VkCommandBuffer cmd) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipe.pipeline);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                              pipe.pipe_layout, 0, 1, &pipe.set, 0,
                              nullptr);
      vkCmdPushConstants(cmd, pipe.pipe_layout,
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
      vkCmdDispatch(cmd, static_cast<uint32_t>((count + 255) / 256), 1, 1);
    });

    // Read back this chunk's flags and journal the corrections.
    std::vector<Point> flags(static_cast<size_t>(count));
    {
      void* mapped = nullptr;
      VK_CHECK(vkMapMemory(ctx.device, pts.mem, base * sizeof(Point),
                           count * sizeof(Point), 0, &mapped));
      std::memcpy(flags.data(), mapped,
                  static_cast<size_t>(count) * sizeof(Point));
      vkUnmapMemory(ctx.device, pts.mem);
    }
    uint64_t chunk_dropped = 0;
    for (uint64_t i = 0; i < count; ++i) {
      if (flags[i].flags & kFlagClipped) {
        EditRec rec{base + i, 1, 0};
        std::fwrite(&rec, sizeof(rec), 1, journal);
        ++chunk_dropped;
      }
    }
    std::fflush(journal);
    dropped += chunk_dropped;
    write_checkpoint(ws.run_checkpoint(run_id), chunk + 1, chunks_total,
                     dropped, attempt);
    log("chunk " + std::to_string(chunk + 1) + "/" +
        std::to_string(chunks_total) + " clipped=" +
        std::to_string(chunk_dropped));
  }
  std::fclose(journal);

  if (interrupted) {
    update_run_json(ws, run_id, "interrupted", attempts, "last_attempt",
                    attempt);
    log("interrupted by user; checkpoint preserved");
    vk_buffer_destroy(ctx, pts);
    vk_buffer_destroy(ctx, order);
    vk_buffer_destroy(ctx, cstart);
    vk_buffer_destroy(ctx, ccount);
    destroy_compute_pipe(ctx, pipe);
    vk_destroy(ctx);
    std::printf("run %s interrupted; resume with: pcdclip run %s %s\n",
                run_id.c_str(), ws.root.c_str(), run_id.c_str());
    return 2;
  }

  // Finalize: apply the journaled corrections onto the duplicate and
  // emit the corrected cloud plus the evidence bundle.
  std::vector<EditRec> edits(dropped);
  {
    FILE* jf = std::fopen(ws.run_journal(run_id).c_str(), "rb");
    if (!jf) die("cannot read journal");
    if (dropped > 0)
      std::fread(edits.data(), sizeof(EditRec), dropped, jf);
    std::fclose(jf);
  }
  for (const auto& e : edits)
    cloud.points[static_cast<size_t>(e.index)].flags |= kFlagClipped;
  save_cloud(ws.run_output(run_id), cloud);

  std::string finished_utc = utc_now_iso();
  std::string out_hash = Sha256::file_hex(ws.run_output(run_id));
  std::string journal_hash = Sha256::file_hex(ws.run_journal(run_id));

  // edits.jsonl: every correction anchored to its original offset.
  {
    std::ostringstream el;
    for (const auto& e : edits) {
      el << "{\"index\": " << e.index
         << ", \"byte_offset\": " << e.index * kPointStride
         << ", \"action\": \"clip\"}\n";
    }
    write_text(ev + "/edits.jsonl", el.str());
  }
  {
    std::ostringstream m;
    m << "{\n"
      << "  \"run_id\": \"" << json_escape(run_id) << "\",\n"
      << "  \"attempt\": \"" << attempt << "\",\n"
      << "  \"total_points\": " << n << ",\n"
      << "  \"clipped_points\": " << dropped << ",\n"
      << "  \"kept_points\": " << (n - dropped) << ",\n"
      << "  \"started_utc\": \"" << started_utc << "\",\n"
      << "  \"finished_utc\": \"" << finished_utc << "\",\n"
      << "  \"chunks_total\": " << chunks_total << ",\n"
      << "  \"chunk_size\": " << chunk_size << "\n"
      << "}\n";
    write_text(ev + "/metrics.json", m.str());
  }
  {
    std::ostringstream m;
    m << "{\n"
      << "  \"run_id\": \"" << json_escape(run_id) << "\",\n"
      << "  \"attempt\": \"" << attempt << "\",\n"
      << "  \"radius\": " << ps.radius << ",\n"
      << "  \"min_neighbors\": " << ps.min_neighbors << ",\n"
      << "  \"max_neighbors\": " << ps.max_neighbors << ",\n"
      << "  \"started_utc\": \"" << started_utc << "\",\n"
      << "  \"finished_utc\": \"" << finished_utc << "\",\n"
      << "  \"vulkan_device\": \"" << json_escape(ctx.device_name)
      << "\",\n"
      << "  \"input_sha256\": \"" << working_hash << "\",\n"
      << "  \"output_sha256\": \"" << out_hash << "\",\n"
      << "  \"journal_sha256\": \"" << journal_hash << "\",\n"
      << "  \"output_points\": " << n << ",\n"
      << "  \"clipped_points\": " << dropped << "\n"
      << "}\n";
    write_text(ev + "/manifest.json", m.str());
  }
  render_snapshot(ctx, cloud, ev + "/render.ppm");
  log("attempt finished, clipped=" + std::to_string(dropped));

  vk_buffer_destroy(ctx, pts);
  vk_buffer_destroy(ctx, order);
  vk_buffer_destroy(ctx, cstart);
  vk_buffer_destroy(ctx, ccount);
  destroy_compute_pipe(ctx, pipe);
  vk_destroy(ctx);

  update_run_json(ws, run_id, "done", attempts, "output_sha256",
                  out_hash);
  std::printf("run %s done: clipped=%llu kept=%llu evidence=%s\n",
              run_id.c_str(), (unsigned long long)dropped,
              (unsigned long long)(n - dropped), ev.c_str());
  return 0;
}

// ---------- export ----------

void run_export(const Workspace& ws, const std::string& run_id,
                const std::string& out_dir) {
  if (!file_exists(ws.run_output(run_id)))
    die("run not finished yet: " + run_id);
  make_dirs(out_dir);
  Cloud c = load_cloud(ws.run_output(run_id));
  Cloud kept;
  kept.points.reserve(c.points.size());
  for (const auto& p : c.points)
    if (!(p.flags & kFlagClipped)) kept.points.push_back(p);
  kept.original_count = kept.points.size();

  std::string bin_path = out_dir + "/" + run_id + "_kept.bin";
  save_cloud(bin_path, kept);
  {
    std::ostringstream xyz;
    for (const auto& p : kept.points)
      xyz << p.x << " " << p.y << " " << p.z << "\n";
    write_text(out_dir + "/" + run_id + "_kept.xyz", xyz.str());
  }
  // Copy the evidence bundle alongside the exported data.
  std::string ev_src = ws.run_evidence(run_id);
  std::string ev_dst = out_dir + "/evidence";
  copy_dir(ev_src, ev_dst);

  std::ostringstream m;
  m << "{\n"
    << "  \"run_id\": \"" << json_escape(run_id) << "\",\n"
    << "  \"exported_utc\": \"" << utc_now_iso() << "\",\n"
    << "  \"kept_points\": " << kept.points.size() << ",\n"
    << "  \"kept_bin_sha256\": \"" << Sha256::file_hex(bin_path)
    << "\",\n"
    << "  \"source_output_sha256\": \""
    << Sha256::file_hex(ws.run_output(run_id)) << "\"\n"
    << "}\n";
  write_text(out_dir + "/export_manifest.json", m.str());
  std::printf("exported run %s to %s\n", run_id.c_str(), out_dir.c_str());
}

} // namespace pcd
