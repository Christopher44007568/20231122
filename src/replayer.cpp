#include "replayer.h"

#include "capture_format.h"
#include "image.h"
#include "renderer.h"
#include "report.h"
#include "vk_context.h"

#include <cstdio>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

uint64_t hashFile(const std::string& path) {
  std::ifstream is(path, std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
  return ptdc::fnv1a64(bytes.data(), bytes.size());
}

std::string addEvidence(Report& report, const std::string& path, const std::string& role) {
  report.evidence.push_back({path, role, hashFile(path)});
  return path;
}

// Extensions this replayer build understands. Anything recorded outside this
// list is preserved and flagged as unknown.
bool isKnownExtension(const std::string& name) {
  return name == "VK_KHR_maintenance1";
}

}  // namespace

int replayCapture(const ReplayOptions& opts) {
  ptdc::Capture cap = ptdc::loadCapture(opts.capturePath);

  // Evidence directory: everything the diagnosis produces or relies on is
  // preserved here before any mutation, so the process stays reviewable.
  char stamp[32];
  std::time_t now = std::time(nullptr);
  std::strftime(stamp, sizeof(stamp), "%Y%m%dT%H%M%SZ", std::gmtime(&now));
  fs::path evDir = fs::path(opts.evidenceRoot) / stamp;
  fs::create_directories(evDir);

  Report report;
  report.title = "图元拓扑诊断报告";
  {
    fs::path dst = evDir / fs::path(opts.capturePath).filename();
    fs::copy_file(opts.capturePath, dst, fs::copy_options::overwrite_existing);
    addEvidence(report, dst.string(), "原始捕获（未修改）");
  }

  const bool legacy = (cap.version == ptdc::kVersionLegacy);
  report.contextLines.push_back(std::string("捕获格式: PTDC v") + std::to_string(cap.version) +
                                (legacy ? "（旧格式，已升级内存模型）" : "（新格式）"));
  report.contextLines.push_back("分辨率: " + std::to_string(cap.width) + "x" +
                                std::to_string(cap.height));
  report.contextLines.push_back("draw call 数: " + std::to_string(cap.draws.size()));

  // --- Unknown extensions -------------------------------------------------
  if (!cap.extensions.empty()) {
    std::string extLog;
    for (const auto& e : cap.extensions) {
      extLog += e.name + " spec=" + std::to_string(e.specVersion) +
                (e.required ? " required" : " optional") + "\n";
      if (!isKnownExtension(e.name)) {
        report.autoFindings.push_back(
            {Severity::Warning, "extension",
             "未知扩展 " + e.name + "（spec v" + std::to_string(e.specVersion) +
                 (e.required ? "，必需" : "，可选") + "）：本重放器未实现，已记录并继续"});
        report.manualChecks.push_back("确认未知扩展 " + e.name +
                                      " 不影响本次重放的画面保真度");
        if (e.required)
          report.nextActions.push_back(
              "扩展 " + e.name + " 标记为必需：在支持该扩展的环境重新录制，或为本重放器实现该扩展处理器");
      }
    }
    std::ofstream(evDir / "extensions.txt") << extLog;
    addEvidence(report, (evDir / "extensions.txt").string(), "扩展记录（含未知扩展）");
  }

  // --- Topology overrides (hypothesis verification) ------------------------
  for (auto& [idx, topo] : opts.topologyOverrides) {
    if (idx >= cap.draws.size()) throw std::runtime_error("override draw index out of range");
    report.contextLines.push_back("假设验证: draw " + std::to_string(idx) + " 拓扑覆盖为 " +
                                  ptdc::topologyName(topo));
    cap.draws[idx].topology = topo;
  }

  // --- Replay --------------------------------------------------------------
  VkContext ctx;
  Renderer renderer(ctx, cap.width, cap.height);
  renderer.uploadVertices(cap.vertices);
  report.contextLines.push_back("重放设备: " + ctx.deviceName());

  std::vector<Image> checkpoints = renderer.renderWithCheckpoints(cap.draws);
  Image final = checkpoints.back();
  savePpm(final, (evDir / "replayed_final.ppm").string());
  addEvidence(report, (evDir / "replayed_final.ppm").string(), "重放最终帧缓冲");

  // Per-draw framebuffer checkpoints: key evidence for draw-call correlation.
  for (size_t i = 0; i < checkpoints.size(); ++i) {
    std::string p = (evDir / ("checkpoint_draw" + std::to_string(i) + ".ppm")).string();
    savePpm(checkpoints[i], p);
    addEvidence(report, p, "draw " + std::to_string(i) + " 执行后的帧缓冲检查点");
  }

  uint64_t finalHash = ptdc::fnv1a64(final.rgba.data(), final.rgba.size());

  // --- Image comparison ----------------------------------------------------
  if (!legacy && !cap.referenceImage.empty()) {
    Image ref{cap.width, cap.height, cap.referenceImage};
    savePpm(ref, (evDir / "reference.ppm").string());
    addEvidence(report, (evDir / "reference.ppm").string(), "捕获内参考画面");

    DiffResult diff = diffImages(final, ref, opts.tolerance);
    if (diff.differingPixels == 0) {
      report.autoFindings.push_back(
          {Severity::Info, "image-diff", "重放画面与参考画面一致（0 像素差异）"});
    } else {
      savePpm(diff.diffImage, (evDir / "diff.ppm").string());
      addEvidence(report, (evDir / "diff.ppm").string(), "画面差异图（红色为差异像素）");
      char msg[256];
      std::snprintf(msg, sizeof(msg),
                    "重放画面与参考画面存在 %llu 个差异像素，最大通道差 %llu，差异区域 (%u,%u)-(%u,%u)",
                    (unsigned long long)diff.differingPixels, (unsigned long long)diff.maxChannelDelta,
                    diff.minX, diff.minY, diff.maxX, diff.maxY);
      report.autoFindings.push_back({Severity::Error, "image-diff", msg});

      // Correlate the differing region back to draw calls. Two signals per
      // draw: (a) pixels it actually painted (checkpoint delta) overlapping
      // the diff, catching wrongly-drawn geometry; (b) its vertex footprint
      // overlapping the diff, catching geometry that went missing (e.g. a
      // topology misread that drops primitives).
      Image black{cap.width, cap.height, std::vector<uint8_t>(cap.width * cap.height * 4, 0)};
      auto isRed = [](const Image& img, size_t p) {
        return img.rgba[p] == 255 && img.rgba[p + 1] == 0 && img.rgba[p + 2] == 0;
      };
      uint64_t bestScore = 0;
      int primarySuspect = -1;
      for (size_t i = 0; i < cap.draws.size(); ++i) {
        const auto& d = cap.draws[i];
        const Image& prev = (i == 0) ? black : checkpoints[i - 1];
        DiffResult painted = diffImages(checkpoints[i], prev, 0);

        // Vertex footprint in pixel space (NDC -> framebuffer).
        float fx0 = 1e9f, fy0 = 1e9f, fx1 = -1e9f, fy1 = -1e9f;
        for (uint32_t vtx = d.firstVertex;
             vtx < d.firstVertex + d.vertexCount && vtx < cap.vertices.size(); ++vtx) {
          float px = (cap.vertices[vtx].pos[0] * 0.5f + 0.5f) * cap.width;
          float py = (cap.vertices[vtx].pos[1] * 0.5f + 0.5f) * cap.height;
          fx0 = std::min(fx0, px);
          fy0 = std::min(fy0, py);
          fx1 = std::max(fx1, px);
          fy1 = std::max(fy1, py);
        }

        uint64_t paintOverlap = 0, footprintOverlap = 0;
        for (uint32_t y = 0; y < cap.height; ++y) {
          for (uint32_t x = 0; x < cap.width; ++x) {
            size_t p = (static_cast<size_t>(y) * cap.width + x) * 4;
            if (!isRed(diff.diffImage, p)) continue;
            if (isRed(painted.diffImage, p)) ++paintOverlap;
            if (x + 0.5f >= fx0 && x + 0.5f <= fx1 && y + 0.5f >= fy0 && y + 0.5f <= fy1)
              ++footprintOverlap;
          }
        }
        uint64_t score = paintOverlap + footprintOverlap;
        if (score > 0) {
          char dmsg[320];
          std::snprintf(dmsg, sizeof(dmsg),
                        "draw %zu（拓扑 %s，顶点 [%u, %u)）：绘制区域命中差异 %llu 像素，顶点包围盒命中 %llu 像素",
                        i, ptdc::topologyName(d.topology), d.firstVertex,
                        d.firstVertex + d.vertexCount, (unsigned long long)paintOverlap,
                        (unsigned long long)footprintOverlap);
          report.autoFindings.push_back({Severity::Error, "draw-correlation", dmsg});
          if (score > bestScore) {
            bestScore = score;
            primarySuspect = static_cast<int>(i);
          }
        }
      }
      if (primarySuspect >= 0) {
        const auto& d = cap.draws[primarySuspect];
        report.autoFindings.push_back(
            {Severity::Error, "draw-correlation",
             "首要怀疑对象: draw " + std::to_string(primarySuspect) + "（拓扑 " +
                 ptdc::topologyName(d.topology) + "），与差异区域重叠最大"});
        report.manualChecks.push_back(
            "查看 diff.ppm 与 reference.ppm，确认 draw " + std::to_string(primarySuspect) +
            " 对应区域的画面差异是否符合拓扑错误的特征");
        report.nextActions.push_back(
            "验证拓扑假设: topo_diag replay " + std::string(fs::path(opts.capturePath).filename()) +
            " --override-topology " + std::to_string(primarySuspect) +
            "=TRIANGLE_LIST（或 STRIP/FAN），观察差异是否消失");
      }
    }
  } else {
    if (finalHash == cap.referenceHash) {
      report.autoFindings.push_back(
          {Severity::Info, "image-diff", "旧格式捕获：重放画面哈希与参考哈希一致"});
    } else {
      report.autoFindings.push_back(
          {Severity::Error, "image-diff",
           "旧格式捕获：重放画面哈希与参考哈希不一致；v1 不含参考图像，无法做逐像素差异定位"});
      report.nextActions.push_back(
          "用新格式重新录制以获得逐像素差异定位：topo_diag record --out <new.ptdc>");
    }
    report.manualChecks.push_back("旧格式捕获仅能校验整体哈希，确认是否需要升级为 v2 重新录制");
  }

  // --- Resource state cross-check -------------------------------------------
  if (!legacy && !cap.resources.empty()) {
    // States this replay actually drove, keyed by resource id.
    std::map<uint32_t, ptdc::ResourceState> actual;
    for (const auto& r : cap.resources) {
      if (r.type == ptdc::ResourceType::Buffer)
        actual[r.id] = ptdc::ResourceState::VertexBuffer;
      else
        actual[r.id] = ptdc::ResourceState::ColorAttachment;
    }
    for (const auto& r : cap.resources) {
      if (r.timeline.empty()) continue;
      ptdc::ResourceState recorded = r.timeline.front().state;
      ptdc::ResourceState got = actual[r.id];
      if (recorded != got) {
        report.autoFindings.push_back(
            {Severity::Warning, "resource-state",
             "资源 " + r.debugName + "（id " + std::to_string(r.id) + "）记录状态 " +
                 ptdc::resourceStateName(recorded) + " 与重放实际状态 " +
                 ptdc::resourceStateName(got) + " 不一致"});
        report.manualChecks.push_back("核对资源 " + r.debugName + " 在录制时的同步/屏障记录");
      }
    }
    // Validate draw -> resource references resolve.
    for (size_t i = 0; i < cap.draws.size(); ++i) {
      for (uint32_t id : cap.draws[i].resourceRefs) {
        bool found = false;
        for (const auto& r : cap.resources)
          if (r.id == id) found = true;
        if (!found)
          report.autoFindings.push_back(
              {Severity::Warning, "resource-state",
               "draw " + std::to_string(i) + " 引用了不存在的资源 id " + std::to_string(id)});
      }
    }
  } else if (legacy) {
    report.autoFindings.push_back(
        {Severity::Info, "resource-state", "旧格式捕获不含资源状态时间线，跳过资源状态核对"});
  }

  // --- Finalize --------------------------------------------------------------
  report.manualChecks.push_back("复核证据目录 " + evDir.string() + " 中的全部证据文件");
  if (report.hasErrors() && opts.topologyOverrides.empty())
    report.nextActions.push_back("按上方建议的 --override-topology 命令验证假设后，将结论回填到人工确认项");
  if (report.nextActions.empty())
    report.nextActions.push_back("无：重放与参考一致，归档证据目录即可");

  writeReport(report, evDir.string());

  std::printf("诊断完成: %s\n", evDir.string().c_str());
  std::printf("  自动结论 %zu 条（错误 %d，警告 %d），人工确认 %zu 项\n",
              report.autoFindings.size(), report.hasErrors() ? 1 : 0,
              report.hasWarnings() ? 1 : 0, report.manualChecks.size());
  std::printf("  报告: %s\n", (evDir / "report.md").string().c_str());
  return report.hasErrors() ? 2 : 0;
}
