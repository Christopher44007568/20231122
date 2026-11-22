#include "report.h"
#include "json.h"
#include "util.h"
#include <sstream>

namespace tcpv {

bool writeReview(const std::string& path, const ReviewFile& r) {
  std::ostringstream o;
  o << "{\n  \"runId\": \"" << jsonEscape(r.runId) << "\",\n  \"items\": [\n";
  for (size_t i = 0; i < r.items.size(); i++) {
    const ReviewItem& it = r.items[i];
    o << "    {\"id\": " << it.id << ", \"text\": \"" << jsonEscape(it.text)
      << "\", \"state\": \"" << it.state << "\", \"reviewer\": \""
      << jsonEscape(it.reviewer) << "\", \"at\": \"" << it.at << "\"}"
      << (i + 1 < r.items.size() ? ",\n" : "\n");
  }
  o << "  ]\n}\n";
  return writeFile(path, o.str());
}

bool readReview(const std::string& path, ReviewFile& r) {
  Json root;
  std::string err;
  if (!parseJson(readFile(path), root, err)) return false;
  r.runId = root.getStr("runId");
  if (const Json* items = root.find("items")) {
    for (const Json& j : items->arr) {
      ReviewItem it;
      it.id = int(j.getNum("id"));
      it.text = j.getStr("text");
      it.state = j.getStr("state", "pending");
      it.reviewer = j.getStr("reviewer");
      it.at = j.getStr("at");
      r.items.push_back(it);
    }
  }
  return true;
}

ReviewFile defaultChecklist(const RunManifest& m) {
  ReviewFile r;
  r.runId = m.runId;
  int id = 1;
  auto add = [&](const std::string& text) {
    r.items.push_back({id++, text, "pending", "", ""});
  };
  add("参考图与原始纹理一致（ref_*.png 无异常色块）");
  for (const auto& v : m.variants)
    add("人工目检 " + v.codec + " 解码图与差异热图（" + v.decPng + " / " +
        v.diffPng + "），确认压缩伪影可接受");
  add("events.jsonl 中 draw_call / resource_state / framebuffer 记录完整且可对应");
  if (m.status == "CANCELLED")
    add("确认中途取消的已完成步骤证据可信，并决定是否重跑完整流程");
  return r;
}

std::vector<std::string> nextSteps(const RunManifest& m, double psnrThreshold) {
  std::vector<std::string> steps;
  if (m.status == "CANCELLED") {
    steps.push_back("重跑完整流程：`tcpv run --texture " + m.texture +
                    "`（不带 --cancel-after）");
    steps.push_back("核对已保留的部分证据：events.jsonl 中 seq 连续的记录、"
                    "manifest.json 的 completedSteps");
    steps.push_back("若取消非预期，检查触发源：" + m.cancelReason);
  }
  if (m.status == "FAILED") {
    steps.push_back("查看 events.jsonl 末尾的 error 事件定位失败点");
    steps.push_back("确认 Vulkan 设备可用：`tcpv doctor`");
  }
  for (const auto& v : m.variants) {
    if (!v.pass) {
      steps.push_back("编解码器 " + v.codec + " PSNR " +
                      formatDouble(v.psnr, 2) + " 低于阈值 " +
                      formatDouble(psnrThreshold, 1) +
                      "：打开差异热图 `" + v.diffPng + "` 定位伪影区域");
      steps.push_back("对 " + v.codec +
                      " 换用更高细节纹理复测：`tcpv run --texture photo "
                      "--codec " +
                      v.codec + "`");
    }
  }
  if (steps.empty())
    steps.push_back("无阻塞问题；完成人工确认清单后即可归档本次运行");
  return steps;
}

std::string renderReport(const RunManifest& m, const ReviewFile& review,
                         double psnrThreshold) {
  std::ostringstream o;
  o << "# 纹理压缩质量预览报告\n\n";
  o << "- 运行 ID：`" << m.runId << "`\n";
  o << "- 状态：**" << m.status << "**\n";
  o << "- 设备：" << m.device << "\n";
  o << "- 纹理：" << m.texture << "（" << m.width << "x" << m.height << "）\n";
  o << "- 开始：" << m.startedAt << "　结束：" << m.finishedAt << "\n";
  if (!m.cancelReason.empty()) o << "- 取消原因：" << m.cancelReason << "\n";
  o << "- PSNR 阈值：" << formatDouble(psnrThreshold, 1) << " dB\n\n";

  o << "## 一、自动结论（机器生成，未含人工判断）\n\n";
  if (m.variants.empty()) {
    o << "本次运行未产生任何编解码变体结果（可能在编码前被取消或失败）。\n\n";
  } else {
    o << "| 编解码器 | 世代 | draw call | PSNR (dB) | 平均绝对误差 | 最大通道误差 | 判定 |\n";
    o << "|---|---|---|---|---|---|---|\n";
    for (const auto& v : m.variants)
      o << "| " << v.codec << " | " << v.generation << " | `" << v.drawCallId
        << "` | " << formatDouble(v.psnr, 2) << " | "
        << formatDouble(v.meanAbsError, 3) << " | " << v.maxChannelError
        << " | " << (v.pass ? "PASS" : "FAIL") << " |\n";
    o << "\n";
    size_t passed = 0;
    for (const auto& v : m.variants) passed += v.pass ? 1 : 0;
    o << "自动判定：" << passed << "/" << m.variants.size()
      << " 个变体达到 PSNR 阈值。旧格式（legacy）与新格式（new）均已覆盖：";
    bool hasLegacy = false, hasNew = false;
    for (const auto& v : m.variants) {
      hasLegacy = hasLegacy || v.generation == "legacy";
      hasNew = hasNew || v.generation == "new";
    }
    o << (hasLegacy ? "legacy✓ " : "legacy✗ ") << (hasNew ? "new✓" : "new✗")
      << "\n\n";
  }

  o << "### 证据对应关系（draw call ↔ 资源状态 ↔ 帧缓冲 ↔ 画面差异）\n\n";
  o << "每个 draw call 的证据链见 `events.jsonl`（按 `drawCall` 字段过滤），"
       "关键图像证据：\n\n";
  for (const auto& v : m.variants) {
    o << "- `" << v.drawCallId << "`（" << v.codec << "）：参考 `"
      << v.refPng << "` / 解码 `" << v.decPng << "` / 差异热图 `"
      << v.diffPng << "`\n";
  }
  o << "\n已完成步骤：";
  for (const auto& s : m.completedSteps) o << "`" << s << "` ";
  if (!m.skippedSteps.empty()) {
    o << "\n\n被跳过步骤（因取消）：";
    for (const auto& s : m.skippedSteps) o << "`" << s << "` ";
  }
  o << "\n\n";

  o << "## 二、人工确认（需复核人填写，机器不做结论）\n\n";
  if (review.items.empty()) {
    o << "（无待确认项）\n\n";
  } else {
    for (const auto& it : review.items) {
      std::string mark = it.state == "approved"    ? "[x]"
                         : it.state == "rejected"  ? "[!]"
                                                   : "[ ]";
      o << "- " << mark << " #" << it.id << " " << it.text;
      if (it.state != "pending")
        o << "（" << it.reviewer << " 于 " << it.at << " 标记为 " << it.state
          << "）";
      o << "\n";
    }
    o << "\n使用 `tcpv confirm --run " << m.runId
      << " --item <编号> --approve|--reject --reviewer <名字>` 更新确认状态。\n\n";
  }

  o << "## 三、后续操作（失败/取消时的可执行步骤）\n\n";
  for (const auto& s : nextSteps(m, psnrThreshold)) o << "1. " << s << "\n";
  o << "\n";

  o << "## 四、证据清单（含 SHA-256）\n\n";
  for (const auto& kv : m.artifacts)
    o << "- `" << kv.first << "` — `" << kv.second << "`\n";
  o << "\n复核方式：`tcpv verify --run " << m.runId
    << "` 重新计算哈希并比对。\n";
  return o.str();
}

} // namespace tcpv
