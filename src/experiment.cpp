#include "experiment.h"
#include "png.h"
#include "sha256.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

bool writeTextFile(const std::string& path, const std::string& text) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
  std::fclose(f);
  return ok;
}

std::string readTextFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string paramsToJson(const SceneParams& p) {
  char buf[512];
  std::snprintf(buf, sizeof(buf),
    "{\"seed\":%u,\"width\":%u,\"height\":%u,\"shadowMapSize\":%u,"
    "\"camYawDeg\":%.4f,\"camPitchDeg\":%.4f,\"camDist\":%.4f,"
    "\"camFovDeg\":%.4f,\"camNear\":%.4f,\"camFar\":%.4f,"
    "\"lightDir\":[%.4f,%.4f,%.4f],\"splitLambda\":%.4f}",
    p.seed, p.width, p.height, p.shadowMapSize, p.camYawDeg, p.camPitchDeg,
    p.camDist, p.camFovDeg, p.camNear, p.camFar, p.lightDir.x, p.lightDir.y,
    p.lightDir.z, p.splitLambda);
  return buf;
}

std::string stateHashOf(const SceneParams& p) {
  std::string s = serializeParams(p);
  return sha256Hex(s.data(), s.size());
}

bool renderAndHash(CsmRenderer& r, const RenderJob& job,
                   const std::string& pngPath, std::string& outHash,
                   std::string& err) {
  std::vector<uint8_t> rgba;
  if (!r.render(job, rgba, err)) return false;
  if (!writePngRgba(pngPath, job.params.width, job.params.height, rgba)) {
    err = "failed to write " + pngPath;
    return false;
  }
  outHash = sha256Hex(rgba.data(), rgba.size());
  return true;
}

} // namespace

std::string serializeParams(const SceneParams& p) {
  char buf[512];
  std::snprintf(buf, sizeof(buf),
    "seed=%u\nwidth=%u\nheight=%u\nshadowMapSize=%u\n"
    "camYawDeg=%.6f\ncamPitchDeg=%.6f\ncamDist=%.6f\ncamFovDeg=%.6f\n"
    "camNear=%.6f\ncamFar=%.6f\nlightDir=%.6f,%.6f,%.6f\nsplitLambda=%.6f\n",
    p.seed, p.width, p.height, p.shadowMapSize, p.camYawDeg, p.camPitchDeg,
    p.camDist, p.camFovDeg, p.camNear, p.camFar, p.lightDir.x, p.lightDir.y,
    p.lightDir.z, p.splitLambda);
  return buf;
}

bool deserializeParams(const std::string& text, SceneParams& out) {
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);
    try {
      if (key == "seed") out.seed = uint32_t(std::stoul(val));
      else if (key == "width") out.width = uint32_t(std::stoul(val));
      else if (key == "height") out.height = uint32_t(std::stoul(val));
      else if (key == "shadowMapSize") out.shadowMapSize = uint32_t(std::stoul(val));
      else if (key == "camYawDeg") out.camYawDeg = std::stof(val);
      else if (key == "camPitchDeg") out.camPitchDeg = std::stof(val);
      else if (key == "camDist") out.camDist = std::stof(val);
      else if (key == "camFovDeg") out.camFovDeg = std::stof(val);
      else if (key == "camNear") out.camNear = std::stof(val);
      else if (key == "camFar") out.camFar = std::stof(val);
      else if (key == "splitLambda") out.splitLambda = std::stof(val);
      else if (key == "lightDir") {
        if (std::sscanf(val.c_str(), "%f,%f,%f", &out.lightDir.x,
                        &out.lightDir.y, &out.lightDir.z) != 3)
          return false;
      }
    } catch (...) {
      return false;
    }
  }
  return true;
}

bool ExperimentReport::allPass() const {
  for (const auto& c : checks)
    if (!c.pass) return false;
  return true;
}

int runExperiment(const ExperimentOptions& opts) {
  namespace fs = std::filesystem;
  const std::string& dir = opts.artifactsDir;
  std::error_code ec;
  fs::create_directories(dir, ec);

  ExperimentReport rep;
  TraceLog trace;
  if (!trace.open(dir + "/trace.jsonl", false)) {
    std::fprintf(stderr, "cannot open trace log in %s\n", dir.c_str());
    return 2;
  }
  std::string err;
  SceneParams params = opts.params;

  auto logStep = [&](const char* run, const char* step, const std::string& input,
                     const std::string& stateHash, const std::string& result,
                     const char* status, const std::string& note) {
    TraceEntry e;
    e.seq = trace.nextSeq();
    e.runId = run;
    e.step = step;
    e.input = input;
    e.stateHash = stateHash;
    e.result = result;
    e.status = status;
    e.note = note;
    trace.write(e);
  };

  // ---------- RECORD RUN ----------
  // Step: glfw_probe (environment evidence; GLFW is the windowing stack).
  {
    std::string glfwNote;
    std::string extsJson = "[";
    if (glfwInit() == GLFW_TRUE) {
      uint32_t n = 0;
      const char** exts = glfwGetRequiredInstanceExtensions(&n);
      for (uint32_t i = 0; i < n; ++i) {
        if (i) extsJson += ",";
        extsJson += "\"" + jsonEscape(exts[i]) + "\"";
      }
      glfwNote = std::string("glfw ") + glfwGetVersionString();
      glfwTerminate();
    } else {
      glfwNote = "glfwInit failed (headless environment); window mode unavailable";
    }
    extsJson += "]";
    logStep("record", "glfw_probe", "{}", "",
            "{\"required_instance_extensions\":" + extsJson + "}", "ok",
            glfwNote);
  }

  // Step: vk_init_control (unknown extension as control group).
  VkContext vk;
  if (!vkInit(vk, {}, true, err)) {
    logStep("record", "vk_init_control", "{}", "", "{}", "fail", err);
    trace.close();
    return 2;
  }
  {
    const ExtControlReport& ec2 = vk.extControl;
    char result[512];
    std::snprintf(result, sizeof(result),
      "{\"unknown_instance_ext\":\"%s\",\"unknown_instance_ext_supported\":%s,"
      "\"unknown_device_ext\":\"%s\",\"unknown_device_ext_supported\":%s,"
      "\"device_create_with_unknown_ext\":%d,\"device_create_clean\":%d,"
      "\"device_name\":\"%s\"}",
      ec2.unknownInstanceExt.c_str(),
      ec2.unknownInstanceExtSupported ? "true" : "false",
      ec2.unknownDeviceExt.c_str(),
      ec2.unknownDeviceExtSupported ? "true" : "false",
      int(ec2.deviceCreateWithUnknownExt), int(ec2.deviceCreateClean),
      jsonEscape(vk.deviceName).c_str());
    logStep("record", "vk_init_control",
            "{\"probe\":\"unknown_extension_control\"}", "", result, "ok", "");
    rep.checks.push_back({"control/unknown_instance_ext_absent",
                          !ec2.unknownInstanceExtSupported, false,
                          ec2.unknownInstanceExt + " supported=" +
                          (ec2.unknownInstanceExtSupported ? "true" : "false")});
    rep.checks.push_back({"control/unknown_device_ext_absent",
                          !ec2.unknownDeviceExtSupported, false,
                          ec2.unknownDeviceExt + " supported=" +
                          (ec2.unknownDeviceExtSupported ? "true" : "false")});
    rep.checks.push_back({"control/device_create_with_unknown_ext_rejected",
                          ec2.deviceCreateWithUnknownExt ==
                              VK_ERROR_EXTENSION_NOT_PRESENT, false,
                          "VkResult=" + std::to_string(int(ec2.deviceCreateWithUnknownExt))});
    rep.checks.push_back({"control/device_create_clean_ok",
                          ec2.deviceCreateClean == VK_SUCCESS, false,
                          "VkResult=" + std::to_string(int(ec2.deviceCreateClean))});
  }

  // Step: renderer_init.
  CsmRenderer renderer;
  if (!renderer.init(vk, params, err)) {
    logStep("record", "renderer_init", paramsToJson(params), "", "{}", "fail", err);
    trace.close();
    vkDestroy(vk);
    return 2;
  }
  logStep("record", "renderer_init", paramsToJson(params), stateHashOf(params),
          "{\"vertex_count\":" + std::to_string(renderer.vertexCount()) + "}",
          "ok", "");

  // Step: render_baseline.
  RenderJob baseJob;
  baseJob.params = params;
  std::string h0;
  if (!renderAndHash(renderer, baseJob, dir + "/baseline.png", h0, err)) {
    logStep("record", "render_baseline", paramsToJson(params), "", "{}", "fail", err);
    trace.close();
    renderer.destroy();
    vkDestroy(vk);
    return 2;
  }
  logStep("record", "render_baseline", paramsToJson(params), stateHashOf(params),
          "{\"image\":\"baseline.png\",\"sha256\":\"" + h0 + "\"}", "ok", "");
  rep.evidence.push_back("baseline.png");

  // Step: snapshot.
  {
    std::string snap = serializeParams(params);
    snap += "baseline_sha256=" + h0 + "\n";
    snap += "device=" + vk.deviceName + "\n";
    snap += "state_hash=" + stateHashOf(params) + "\n";
    if (!writeTextFile(dir + "/snapshot.state", snap)) {
      logStep("record", "snapshot", "{}", "", "{}", "fail", "write failed");
      trace.close();
      renderer.destroy();
      vkDestroy(vk);
      return 2;
    }
    logStep("record", "snapshot", paramsToJson(params), stateHashOf(params),
            "{\"file\":\"snapshot.state\",\"baseline_sha256\":\"" + h0 + "\"}",
            "ok", "");
    rep.evidence.push_back("snapshot.state");
  }

  renderer.destroy();
  vkDestroy(vk);

  // ---------- REPLAY RUN (restore from snapshot) ----------
  SceneParams restored;
  std::string recordedBaselineHash, recordedStateHash;
  {
    std::string snap = readTextFile(dir + "/snapshot.state");
    if (snap.empty() || !deserializeParams(snap, restored)) {
      logStep("replay", "restore", "{}", "", "{}", "fail",
              "snapshot.state missing or corrupt");
      trace.close();
      return 2;
    }
    std::istringstream in(snap);
    std::string line;
    while (std::getline(in, line)) {
      if (line.rfind("baseline_sha256=", 0) == 0)
        recordedBaselineHash = line.substr(16);
      else if (line.rfind("state_hash=", 0) == 0)
        recordedStateHash = line.substr(11);
    }
    bool stateOk = (stateHashOf(restored) == recordedStateHash);
    logStep("replay", "restore", "{\"file\":\"snapshot.state\"}",
            stateHashOf(restored),
            "{\"recorded_state_hash\":\"" + recordedStateHash + "\"}",
            stateOk ? "ok" : "fail",
            stateOk ? "" : "state hash mismatch after restore");
    rep.checks.push_back({"replay/state_hash_match", stateOk, false,
                          "restored=" + stateHashOf(restored)});
  }

  VkContext vk2;
  if (!vkInit(vk2, {}, false, err)) {
    logStep("replay", "vk_reinit", "{}", "", "{}", "fail", err);
    trace.close();
    return 2;
  }
  CsmRenderer renderer2;
  if (!renderer2.init(vk2, restored, err)) {
    logStep("replay", "renderer_reinit", paramsToJson(restored), "", "{}", "fail", err);
    trace.close();
    vkDestroy(vk2);
    return 2;
  }
  logStep("replay", "renderer_reinit", paramsToJson(restored),
          stateHashOf(restored), "{}", "ok", "");

  // Step: replay_render (must reproduce the baseline bit-exactly).
  RenderJob replayJob;
  replayJob.params = restored;
  std::string h1;
  if (!renderAndHash(renderer2, replayJob, dir + "/replay.png", h1, err)) {
    logStep("replay", "replay_render", paramsToJson(restored), "", "{}", "fail", err);
    trace.close();
    renderer2.destroy();
    vkDestroy(vk2);
    return 2;
  }
  logStep("replay", "replay_render", paramsToJson(restored), stateHashOf(restored),
          "{\"image\":\"replay.png\",\"sha256\":\"" + h1 + "\"}", "ok", "");
  rep.evidence.push_back("replay.png");
  rep.checks.push_back({"replay/image_deterministic", h1 == h0, false,
                        "record=" + h0.substr(0, 16) + " replay=" + h1.substr(0, 16)});

  // ---------- REORDER RUN ----------
  // Independent steps: cascade render order permuted -> identical result.
  RenderJob permJob;
  permJob.params = restored;
  permJob.cascadeOrder = {2, 0, 1};
  std::string h2;
  if (!renderAndHash(renderer2, permJob, dir + "/reorder_permuted.png", h2, err)) {
    logStep("reorder", "cascade_order_permuted", "{}", "", "{}", "fail", err);
    trace.close();
    renderer2.destroy();
    vkDestroy(vk2);
    return 2;
  }
  logStep("reorder", "cascade_order_permuted",
          "{\"cascade_order\":[2,0,1]}", stateHashOf(restored),
          "{\"image\":\"reorder_permuted.png\",\"sha256\":\"" + h2 + "\"}",
          "ok", "");
  rep.evidence.push_back("reorder_permuted.png");
  rep.checks.push_back({"reorder/cascade_order_independent", h2 == h0, false,
                        "permuted=" + h2.substr(0, 16)});

  // Dependent steps (negative control): stale shadow maps from a different
  // light, then scene-before-shadows -> result must differ from baseline.
  RenderJob staleJob;
  staleJob.params = restored;
  staleJob.params.lightDir = {-restored.lightDir.z, restored.lightDir.y,
                              restored.lightDir.x};
  std::string hStale;
  if (!renderAndHash(renderer2, staleJob, dir + "/order_negative_prepare.png",
                     hStale, err)) {
    trace.close();
    renderer2.destroy();
    vkDestroy(vk2);
    return 2;
  }
  logStep("reorder", "order_negative_prepare", paramsToJson(staleJob.params),
          stateHashOf(staleJob.params),
          "{\"image\":\"order_negative_prepare.png\",\"sha256\":\"" + hStale + "\"}",
          "ok", "fills shadow maps with mismatched light state");

  RenderJob sceneFirstJob;
  sceneFirstJob.params = restored;
  sceneFirstJob.sceneBeforeShadows = true;
  std::string h3;
  if (!renderAndHash(renderer2, sceneFirstJob, dir + "/order_negative_scene_first.png",
                     h3, err)) {
    trace.close();
    renderer2.destroy();
    vkDestroy(vk2);
    return 2;
  }
  logStep("reorder", "order_negative_scene_first",
          "{\"scene_before_shadows\":true}", stateHashOf(restored),
          "{\"image\":\"order_negative_scene_first.png\",\"sha256\":\"" + h3 + "\"}",
          "expected_fail", "dependent-step order change must alter the result");
  rep.evidence.push_back("order_negative_scene_first.png");
  rep.checks.push_back({"reorder/dependent_order_sensitive", h3 != h0, true,
                        "scene_first=" + h3.substr(0, 16) +
                        " (expected to differ from baseline)"});

  // ---------- DUPLICATE RUN ----------
  // Same recorded command buffer submitted twice -> identical results.
  std::string h4a, h4b;
  {
    RenderJob dupJob;
    dupJob.params = restored;
    if (!renderer2.record(dupJob, err)) {
      logStep("duplicate", "duplicate_submit", "{}", "", "{}", "fail", err);
      trace.close();
      renderer2.destroy();
      vkDestroy(vk2);
      return 2;
    }
    std::vector<uint8_t> rgba1, rgba2;
    if (!renderer2.submitAndReadback(rgba1, err) ||
        !renderer2.submitAndReadback(rgba2, err)) {
      logStep("duplicate", "duplicate_submit", "{}", "", "{}", "fail", err);
      trace.close();
      renderer2.destroy();
      vkDestroy(vk2);
      return 2;
    }
    writePngRgba(dir + "/duplicate_1.png", restored.width, restored.height, rgba1);
    writePngRgba(dir + "/duplicate_2.png", restored.width, restored.height, rgba2);
    h4a = sha256Hex(rgba1.data(), rgba1.size());
    h4b = sha256Hex(rgba2.data(), rgba2.size());
    logStep("duplicate", "duplicate_submit",
            "{\"submissions\":2,\"same_command_buffer\":true}",
            stateHashOf(restored),
            "{\"first\":\"" + h4a + "\",\"second\":\"" + h4b + "\"}", "ok", "");
    rep.evidence.push_back("duplicate_1.png");
    rep.evidence.push_back("duplicate_2.png");
    rep.checks.push_back({"duplicate/submit_idempotent", h4a == h4b, false,
                          "first=" + h4a.substr(0, 16) +
                          " second=" + h4b.substr(0, 16)});
    rep.checks.push_back({"duplicate/consistent_with_baseline", h4a == h0, false,
                          "first=" + h4a.substr(0, 16)});
  }

  std::string deviceName = vk2.deviceName;
  renderer2.destroy();
  vkDestroy(vk2);
  {
    std::string hashes = "{\n";
    auto addHash = [&](const char* file, const std::string& h) {
      hashes += "  \"" + std::string(file) + "\": \"" + h + "\",\n";
    };
    addHash("baseline.png", h0);
    addHash("replay.png", h1);
    addHash("reorder_permuted.png", h2);
    addHash("order_negative_scene_first.png", h3);
    addHash("duplicate_1.png", h4a);
    addHash("duplicate_2.png", h4b);
    hashes += "  \"state_hash\": \"" + stateHashOf(restored) + "\"\n}\n";
    writeTextFile(dir + "/hashes.json", hashes);
    rep.evidence.push_back("hashes.json");
    rep.evidence.push_back("trace.jsonl");
  }

  // ---------- Human confirmation items ----------
  rep.humanItems = {
    "打开 artifacts/baseline.png，确认近/中/远三个级联分别呈红/绿/蓝色调，且边界清晰",
    "确认 baseline.png 中阴影投射方向与输入 lightDir 一致",
    "对比 order_negative_scene_first.png 与 baseline.png，确认存在肉眼可见的阴影异常",
    "抽查 trace.jsonl 中至少 3 个步骤，确认 input/state_hash/result 字段完整",
    "确认 snapshot.state 中的参数与本次实验设定一致",
    "人工签字: ____________  日期: ____________",
  };

  // ---------- Executable follow-ups when any check fails ----------
  if (!rep.allPass()) {
    rep.nextSteps = {
      "grep '\"status\":\"fail\"' artifacts/trace.jsonl   # 定位失败步骤",
      "VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ./build/csm_lab run   # 强制软件 Vulkan 重试",
      "./build/csm_lab run --artifacts artifacts_retry   # 隔离目录完整重跑",
      "./build/csm_lab present   # 开窗人工检查级联可视化",
      "ls -l artifacts/ && cat artifacts/hashes.json   # 核对证据完整性",
    };
  }

  // ---------- Report ----------
  {
    std::string md;
    md += "# 阴影级联可视化可回放实验报告\n\n";
    md += "- 设备: `" + deviceName + "`\n";
    md += "- 参数: `" + paramsToJson(restored) + "`\n";
    md += "- 状态哈希: `" + stateHashOf(restored) + "`\n\n";

    md += "## 自动结论\n\n";
    md += "| 检查项 | 结果 | 说明 |\n|---|---|---|\n";
    for (const auto& c : rep.checks) {
      md += "| " + c.name + " | " + (c.pass ? "PASS" : "FAIL");
      if (c.expectedNegative) md += " (负对照)";
      md += " | " + c.detail + " |\n";
    }
    md += "\n总体结论: **";
    md += rep.allPass() ? "PASS（全部自动检查通过）" : "FAIL（存在未通过项）";
    md += "**\n\n";

    md += "## 关键证据\n\n";
    for (const auto& e : rep.evidence)
      md += "- `artifacts/" + e + "`\n";
    md += "\n";

    md += "## 人工确认\n\n";
    for (const auto& h : rep.humanItems)
      md += "- [ ] " + h + "\n";
    md += "\n";

    if (!rep.nextSteps.empty()) {
      md += "## 失败后续操作（可执行）\n\n```sh\n";
      for (const auto& s : rep.nextSteps) md += s + "\n";
      md += "```\n";
    }
    writeTextFile(dir + "/report.md", md);
  }

  trace.close();
  std::printf("experiment %s: %zu checks, report at %s/report.md\n",
              rep.allPass() ? "PASS" : "FAIL", rep.checks.size(), dir.c_str());
  return rep.allPass() ? 0 : 1;
}
