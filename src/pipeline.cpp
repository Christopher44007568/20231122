#include "pipeline.h"
#include "evidence.h"
#include "report.h"
#include "textures.h"
#include "util.h"
#include "vk.h"
#include <cstdio>

namespace tcpv {
namespace {

std::string makeRunId(const RunOptions& opt) {
  if (!opt.runId.empty()) return opt.runId;
  std::string ts = nowIso8601();
  for (auto& c : ts)
    if (c == ':' || c == '-') c = '_';
  return ts + "_" + opt.texture;
}

struct CancelCheck {
  const RunOptions& opt;
  VkEngine& engine;
  int drawsDone = 0;
  std::string reason;

  // Called after each draw call and between steps.
  bool cancelled() {
    if (!reason.empty()) return true;
    if (opt.cancelAfterDraws >= 0 && drawsDone >= opt.cancelAfterDraws) {
      reason = "cancel-after=" + std::to_string(opt.cancelAfterDraws) +
               " draw calls (scripted mid-run cancellation)";
      return true;
    }
    if (engine.cancelRequested()) {
      reason = "user requested cancel (ESC/window close)";
      return true;
    }
    return false;
  }
};

} // namespace

int runPreview(const RunOptions& opt) {
  std::string runId = makeRunId(opt);
  std::string dir = opt.runsDir + "/" + runId;
  makeDirs(dir);

  EventLog log;
  if (!log.open(dir + "/events.jsonl")) {
    std::fprintf(stderr, "cannot open event log in %s\n", dir.c_str());
    return 2;
  }

  RunManifest m;
  m.runId = runId;
  m.startedAt = nowIso8601();
  m.texture = opt.texture;
  m.width = opt.width;
  m.height = opt.height;
  m.status = "FAILED";

  std::vector<std::string> allSteps = {"load", "encode", "upload", "draw",
                                       "capture", "diff", "report"};
  auto finishStep = [&](const std::string& s) { m.completedSteps.push_back(s); };

  log.lifecycle("run_start", "runId=" + runId + " texture=" + opt.texture);

  VkEngine engine;
  if (!engine.init(opt.windowed, log)) {
    log.error("vulkan_init", engine.error());
    m.finishedAt = nowIso8601();
    writeManifest(dir + "/manifest.json", m);
    std::fprintf(stderr, "vulkan init failed: %s\n", engine.error().c_str());
    return 3;
  }
  m.device = engine.deviceName();

  CancelCheck cancel{opt, engine, 0, ""};

  // Step: load (generate) source texture.
  Image src = makeSourceTexture(opt.texture, opt.width, opt.height);
  writePng(dir + "/src.png", src);
  finishStep("load");
  log.lifecycle("step", "load complete");

  std::vector<CodecId> codecs = opt.codecs;
  if (codecs.empty())
    for (const auto& c : allCodecs()) codecs.push_back(c.id);

  // Reference draw: uncompressed source rendered through the same pipeline.
  GpuTexture refTex;
  RenderResult refRR;
  bool cancelled = false;

  auto doDraw = [&](const std::string& tag, const Image& img, GpuTexture& tex,
                    RenderResult& rr) -> bool {
    tex = engine.uploadTexture("tex/" + tag, img);
    if (tex.image == 0) {
      log.error("upload", "texture upload failed for " + tag);
      return false;
    }
    rr = engine.renderQuad(tex, opt.width, opt.height, "draw/" + tag);
    if (!rr.ok) {
      log.error("draw", rr.error);
      return false;
    }
    cancel.drawsDone++;
    return true;
  };

  if (!doDraw("ref", src, refTex, refRR)) goto done;
  writePng(dir + "/ref.png", refRR.captured);
  log.capture("draw/ref", "ref.png", sha256File(dir + "/ref.png"));
  finishStep("upload");
  finishStep("draw");
  finishStep("capture");

  for (CodecId cid : codecs) {
    if (cancel.cancelled()) {
      cancelled = true;
      break;
    }
    const CodecInfo& ci = codecInfo(cid);
    std::string tag = ci.name;

    std::vector<uint8_t> blocks = encodeBlocks(cid, src);
    writeFileBytes(dir + "/encoded_" + tag + ".bin", blocks);
    Image decoded = decodeBlocks(cid, blocks.data(), opt.width, opt.height);

    GpuTexture tex;
    RenderResult rr;
    if (!doDraw(tag, decoded, tex, rr)) goto done;

    std::string decPng = "dec_" + tag + ".png";
    std::string diffPng = "diff_" + tag + ".png";
    writePng(dir + "/" + decPng, rr.captured);
    log.capture(rr.drawCallId, decPng, sha256File(dir + "/" + decPng));

    Image diff = makeDiffHeatmap(refRR.captured, rr.captured);
    writePng(dir + "/" + diffPng, diff);

    VariantMetrics vm;
    vm.codec = ci.name;
    vm.generation = ci.generation;
    vm.drawCallId = rr.drawCallId;
    vm.psnr = computePsnr(refRR.captured, rr.captured);
    vm.meanAbsError = computeMeanAbsError(refRR.captured, rr.captured);
    vm.maxChannelError = computeMaxChannelError(refRR.captured, rr.captured);
    vm.pass = vm.psnr >= opt.psnrThreshold;
    vm.refPng = "ref.png";
    vm.decPng = decPng;
    vm.diffPng = diffPng;
    m.variants.push_back(vm);
    log.metric(rr.drawCallId, "psnr_db", vm.psnr);
    log.metric(rr.drawCallId, "mean_abs_error", vm.meanAbsError);
    engine.destroyTexture(tex);
    if (m.variants.size() == 1) {
      finishStep("encode");
      finishStep("diff");
    }
  }

done:
  if (refTex.image) engine.destroyTexture(refTex);

  if (cancelled || cancel.cancelled()) {
    m.status = "CANCELLED";
    m.cancelReason = cancel.reason;
    log.cancellation("draw", cancel.reason);
  } else if (m.variants.size() == codecs.size()) {
    m.status = "COMPLETED";
  }
  log.lifecycle("run_end", "status=" + m.status);
  // Evidence artifacts with hashes (everything except the manifest itself).
  auto hashIf = [&](const std::string& name) {
    if (fileExists(dir + "/" + name))
      m.artifacts[name] = sha256File(dir + "/" + name);
  };
  hashIf("events.jsonl");
  hashIf("src.png");
  hashIf("ref.png");
  for (const auto& v : m.variants) {
    hashIf(v.decPng);
    hashIf(v.diffPng);
    hashIf("encoded_" + v.codec + ".bin");
  }

  ReviewFile review;
  if (!readReview(dir + "/review.json", review)) {
    review = defaultChecklist(m);
    writeReview(dir + "/review.json", review);
  }
  m.artifacts["review.json"] = sha256File(dir + "/review.json");

  m.finishedAt = nowIso8601();
  finishStep("report");
  for (const std::string& s : allSteps) {
    bool done_ = false;
    for (const auto& c : m.completedSteps)
      if (c == s) done_ = true;
    if (!done_) m.skippedSteps.push_back(s);
  }
  writeManifest(dir + "/manifest.json", m);
  writeFile(dir + "/report.md", renderReport(m, review, opt.psnrThreshold));

  engine.shutdown();
  std::printf("run %s: %s (%zu variants) -> %s\n", runId.c_str(),
              m.status.c_str(), m.variants.size(), dir.c_str());
  return m.status == "COMPLETED" ? 0 : (m.status == "CANCELLED" ? 4 : 3);
}

int cmdConfirm(const std::string& runsDir, const std::string& runId, int itemId,
               bool approve, const std::string& reviewer) {
  std::string dir = runsDir + "/" + runId;
  ReviewFile review;
  if (!readReview(dir + "/review.json", review)) {
    std::fprintf(stderr, "no review.json for run %s\n", runId.c_str());
    return 2;
  }
  bool found = false;
  for (auto& it : review.items) {
    if (it.id == itemId) {
      it.state = approve ? "approved" : "rejected";
      it.reviewer = reviewer;
      it.at = nowIso8601();
      found = true;
    }
  }
  if (!found) {
    std::fprintf(stderr, "no item #%d in checklist\n", itemId);
    return 2;
  }
  writeReview(dir + "/review.json", review);

  RunManifest m;
  std::string err;
  if (!readManifest(dir + "/manifest.json", m, err)) {
    std::fprintf(stderr, "manifest unreadable: %s\n", err.c_str());
    return 2;
  }
  writeFile(dir + "/report.md", renderReport(m, review, 30.0));
  std::printf("item #%d -> %s by %s\n", itemId,
              approve ? "approved" : "rejected", reviewer.c_str());
  return 0;
}

int cmdVerify(const std::string& runsDir, const std::string& runId) {
  std::string dir = runsDir + "/" + runId;
  RunManifest m;
  std::string err;
  if (!readManifest(dir + "/manifest.json", m, err)) {
    std::fprintf(stderr, "manifest unreadable: %s\n", err.c_str());
    return 2;
  }
  std::printf("run %s: schema=v%d status=%s variants=%zu\n", runId.c_str(),
              m.schema, m.status.c_str(), m.variants.size());
  if (m.schema == 1) {
    std::printf("legacy v1 manifest: no artifact hashes recorded; "
                "hash check skipped, structural check only\n");
    return 0;
  }
  int bad = 0;
  for (const auto& kv : m.artifacts) {
    std::string path = dir + "/" + kv.first;
    if (!fileExists(path)) {
      std::printf("MISSING  %s\n", kv.first.c_str());
      bad++;
      continue;
    }
    std::string h = sha256File(path);
    if (h != kv.second) {
      std::printf("MISMATCH %s\n  recorded %s\n  actual   %s\n",
                  kv.first.c_str(), kv.second.c_str(), h.c_str());
      bad++;
    } else {
      std::printf("OK       %s\n", kv.first.c_str());
    }
  }
  std::printf(bad ? "verify: %d artifact(s) failed\n" : "verify: all OK\n",
              bad);
  return bad ? 5 : 0;
}

int cmdReport(const std::string& runsDir, const std::string& runId) {
  std::string dir = runsDir + "/" + runId;
  RunManifest m;
  std::string err;
  if (!readManifest(dir + "/manifest.json", m, err)) {
    std::fprintf(stderr, "manifest unreadable: %s\n", err.c_str());
    return 2;
  }
  ReviewFile review;
  if (!readReview(dir + "/review.json", review)) review = defaultChecklist(m);
  writeFile(dir + "/report.md", renderReport(m, review, 30.0));
  std::printf("report regenerated: %s/report.md\n", dir.c_str());
  return 0;
}

int cmdDoctor() {
  EventLog log;
  VkEngine engine;
  EventLog sink;
  (void)log;
  if (!engine.init(false, sink)) {
    std::printf("doctor: Vulkan unavailable: %s\n", engine.error().c_str());
    return 3;
  }
  std::printf("doctor: Vulkan OK, device = %s\n", engine.deviceName().c_str());
  engine.shutdown();
  return 0;
}

} // namespace tcpv
