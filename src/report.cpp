#include "report.h"

#include <vulkan/vulkan.h>

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace audit {

namespace {

std::string apiVersionString(uint32_t version) {
  std::ostringstream oss;
  oss << VK_API_VERSION_MAJOR(version) << "."
      << VK_API_VERSION_MINOR(version) << "."
      << VK_API_VERSION_PATCH(version);
  return oss.str();
}

std::string versionHexLocal(uint32_t version) {
  std::ostringstream oss;
  oss << "0x" << std::hex << std::setw(8) << std::setfill('0')
      << version;
  return oss.str();
}

std::string jsonEscape(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (char ch : input) {
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          std::ostringstream oss;
          oss << "\\u" << std::hex << std::setw(4)
              << std::setfill('0') << static_cast<int>(ch);
          out += oss.str();
        } else {
          out += ch;
        }
    }
  }
  return out;
}

std::string htmlEscape(const std::string& input) {
  std::string out;
  for (char ch : input) {
    switch (ch) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out += ch;
    }
  }
  return out;
}

std::string verdictClass(AutoVerdict verdict) {
  switch (verdict) {
    case AutoVerdict::Occluded:
      return "verdict-occluded";
    case AutoVerdict::NotOccluded:
      return "verdict-visible";
    case AutoVerdict::Cancelled:
      return "verdict-cancelled";
    case AutoVerdict::Undecidable:
      return "verdict-unknown";
  }
  return "verdict-unknown";
}

std::string verdictLabel(AutoVerdict verdict) {
  switch (verdict) {
    case AutoVerdict::Occluded:
      return "自动结论：被遮挡";
    case AutoVerdict::NotOccluded:
      return "自动结论：未遮挡";
    case AutoVerdict::Cancelled:
      return "自动结论：中途取消";
    case AutoVerdict::Undecidable:
      return "自动结论：无法判断";
  }
  return "自动结论：未知";
}

std::string vkResultNameLocal(VkResult result) {
  switch (result) {
    case VK_SUCCESS:
      return "VK_SUCCESS";
    case VK_NOT_READY:
      return "VK_NOT_READY";
    case VK_TIMEOUT:
      return "VK_TIMEOUT";
    case VK_INCOMPLETE:
      return "VK_INCOMPLETE";
    default: {
      std::ostringstream oss;
      oss << "VkResult(" << static_cast<int>(result) << ")";
      return oss.str();
    }
  }
}

}  // namespace

std::string buildRawInputLedger(const AuditBundle& bundle) {
  std::ostringstream oss;
  oss << "# 遮挡查询原始输入账本\n";
  oss << "run_id: " << bundle.runId << "\n";
  oss << "created_at_utc: " << bundle.createdAtUtc << "\n";
  oss << "device: " << bundle.environment.deviceName << "\n";
  oss << "---\n";
  for (const auto& rec : bundle.scenarios) {
    oss << "[" << rec.id << "] " << rec.title << "\n";
    oss << "  query_slot: " << rec.input.slot << "\n";
    oss << "  label: " << rec.input.label << "\n";
    oss << "  expected_use: " << rec.input.expectedUse << "\n";
    oss << "  begin_recorded: "
        << (rec.input.beginRecorded ? "true" : "false") << "\n";
    oss << "  end_recorded: "
        << (rec.input.endRecorded ? "true" : "false") << "\n";
    oss << "  submitted: "
        << (rec.input.submitted ? "true" : "false") << "\n";
  }
  return oss.str();
}

std::string buildJsonReport(const AuditBundle& bundle) {
  std::ostringstream oss;
  oss << "{\n";
  oss << "  \"run_id\": \"" << jsonEscape(bundle.runId) << "\",\n";
  oss << "  \"created_at_utc\": \"" << jsonEscape(bundle.createdAtUtc)
      << "\",\n";
  oss << "  \"input_sha256\": \"" << jsonEscape(bundle.inputSha256)
      << "\",\n";
  oss << "  \"ledger_sha256\": \"" << jsonEscape(bundle.ledgerSha256)
      << "\",\n";
  oss << "  \"environment\": {\n";
  oss << "    \"glfw_version\": \""
      << jsonEscape(bundle.environment.glfwVersion) << "\",\n";
  oss << "    \"instance_api\": \""
      << jsonEscape(bundle.environment.instanceApi) << "\",\n";
  oss << "    \"device_name\": \""
      << jsonEscape(bundle.environment.deviceName) << "\",\n";
  oss << "    \"api_version\": \""
      << jsonEscape(apiVersionString(bundle.environment.apiVersion))
      << "\",\n";
  oss << "    \"driver_version\": \""
      << jsonEscape(versionHexLocal(bundle.environment.driverVersion))
      << "\",\n";
  oss << "    \"hidden_window\": "
      << (bundle.environment.hiddenWindow ? "true" : "false") << ",\n";
  oss << "    \"surface_supported\": "
      << (bundle.environment.surfaceSupported ? "true" : "false") << ",\n";
  oss << "    \"window_mode_reason\": \""
      << jsonEscape(bundle.environment.windowModeReason) << "\"\n";
  oss << "  },\n";
  oss << "  \"scenarios\": [\n";
  for (size_t i = 0; i < bundle.scenarios.size(); ++i) {
    const auto& rec = bundle.scenarios[i];
    oss << "    {\n";
    oss << "      \"id\": \"" << jsonEscape(rec.id) << "\",\n";
    oss << "      \"kind\": \"" << toString(rec.kind) << "\",\n";
    oss << "      \"title\": \"" << jsonEscape(rec.title) << "\",\n";
    oss << "      \"raw_input\": {\n";
    oss << "        \"slot\": " << rec.input.slot << ",\n";
    oss << "        \"label\": \"" << jsonEscape(rec.input.label)
        << "\",\n";
    oss << "        \"expected_use\": \""
        << jsonEscape(rec.input.expectedUse) << "\",\n";
    oss << "        \"begin_recorded\": "
        << (rec.input.beginRecorded ? "true" : "false") << ",\n";
    oss << "        \"end_recorded\": "
        << (rec.input.endRecorded ? "true" : "false") << ",\n";
    oss << "        \"submitted\": "
        << (rec.input.submitted ? "true" : "false") << "\n";
    oss << "      },\n";
    oss << "      \"processing_steps\": [\n";
    for (size_t j = 0; j < rec.steps.size(); ++j) {
      const auto& step = rec.steps[j];
      oss << "        {\"order\": " << step.order << ", \"stage\": \""
          << jsonEscape(step.stage) << "\", \"detail\": \""
          << jsonEscape(step.detail) << "\", \"evidence_ref\": \""
          << jsonEscape(step.evidenceRef) << "\"}"
          << (j + 1 < rec.steps.size() ? "," : "") << "\n";
    }
    oss << "      ],\n";
    oss << "      \"query_result\": {\n";
    oss << "        \"slot\": " << rec.result.slot << ",\n";
    oss << "        \"vk_result\": " << rec.result.vkResult << ",\n";
    oss << "        \"sample_count\": " << rec.result.sampleCount << ",\n";
    oss << "        \"availability\": " << rec.result.availability << ",\n";
    oss << "        \"result_flags\": " << rec.result.resultFlags << ",\n";
    oss << "        \"readable\": "
        << (rec.result.readable ? "true" : "false") << "\n";
    oss << "      },\n";
    oss << "      \"auto_conclusion\": {\n";
    oss << "        \"verdict\": \"" << toString(rec.verdict) << "\",\n";
    oss << "        \"reason\": \"" << jsonEscape(rec.verdictReason)
        << "\"\n";
    oss << "      },\n";
    oss << "      \"manual_review\": {\n";
    oss << "        \"status\": \"" << jsonEscape(rec.manualStatus)
        << "\",\n";
    oss << "        \"note\": \"" << jsonEscape(rec.manualNote) << "\"\n";
    oss << "      },\n";
    oss << "      \"follow_up_actions\": [\n";
    for (size_t j = 0; j < rec.followUpActions.size(); ++j) {
      oss << "        \"" << jsonEscape(rec.followUpActions[j]) << "\""
          << (j + 1 < rec.followUpActions.size() ? "," : "") << "\n";
    }
    oss << "      ]\n";
    oss << "    }" << (i + 1 < bundle.scenarios.size() ? "," : "")
        << "\n";
  }
  oss << "  ]\n";
  oss << "}\n";
  return oss.str();
}

namespace {

const char* kPageStyle = R"CSS(
:root {
  --bg: #0f1420;
  --panel: #171e2e;
  --panel-2: #1d2639;
  --line: #2a3650;
  --text: #e6ebf5;
  --muted: #93a0b8;
  --accent: #5aa7ff;
  --green: #4cc38a;
  --amber: #f0b457;
  --red: #ef6f6f;
  --purple: #b48cf2;
}
* { box-sizing: border-box; }
body {
  margin: 0;
  background: var(--bg);
  color: var(--text);
  font: 14px/1.6 -apple-system, "Segoe UI", "Noto Sans CJK SC",
    "Microsoft YaHei", sans-serif;
}
header {
  padding: 28px 36px 18px;
  border-bottom: 1px solid var(--line);
  background: linear-gradient(135deg, #141c2e, #101623);
}
h1 { margin: 0 0 6px; font-size: 24px; }
h2 { font-size: 17px; margin: 0 0 14px; }
h3 { margin: 0 0 10px; font-size: 15px; }
.sub { color: var(--muted); font-size: 13px; }
main { padding: 28px 36px 48px; max-width: 1180px; margin: 0 auto; }
.grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 18px; }
.card {
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 12px;
  padding: 18px 20px;
  margin-bottom: 18px;
}
.meta {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
  margin-top: 14px;
}
.meta div {
  background: var(--panel-2);
  border: 1px solid var(--line);
  border-radius: 8px;
  padding: 10px 12px;
}
.meta span { display: block; color: var(--muted); font-size: 12px; }
.meta b { font-size: 13px; word-break: break-all; }
.badge {
  display: inline-block;
  padding: 3px 10px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 600;
}
.verdict-visible .badge { background: rgba(76,195,138,.16); color: var(--green); }
.verdict-occluded .badge { background: rgba(240,180,87,.16); color: var(--amber); }
.verdict-cancelled .badge { background: rgba(239,111,111,.16); color: var(--red); }
.verdict-unknown .badge { background: rgba(180,140,242,.18); color: var(--purple); }
.section-title {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 12px;
}
.auto-zone, .manual-zone {
  border-radius: 10px;
  padding: 14px 16px;
  margin-top: 12px;
}
.auto-zone {
  background: rgba(90,167,255,.08);
  border: 1px solid rgba(90,167,255,.35);
}
.manual-zone {
  background: rgba(180,140,242,.08);
  border: 1px solid rgba(180,140,242,.32);
}
table { width: 100%; border-collapse: collapse; font-size: 13px; }
th, td {
  text-align: left;
  padding: 8px 10px;
  border-bottom: 1px solid var(--line);
  vertical-align: top;
}
th { color: var(--muted); font-weight: 600; }
code {
  background: #0c111d;
  border: 1px solid var(--line);
  border-radius: 5px;
  padding: 1px 6px;
  font-family: ui-monospace, "JetBrains Mono", Menlo, monospace;
  font-size: 12px;
}
.steps { margin: 0; padding-left: 0; list-style: none; }
.steps li {
  padding: 8px 10px;
  border-left: 3px solid var(--accent);
  background: var(--panel-2);
  border-radius: 0 8px 8px 0;
  margin-bottom: 8px;
}
.steps .ref { color: var(--muted); font-size: 12px; }
.evidence-row {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 10px;
  margin-top: 10px;
}
.evidence-row div {
  background: #0c111d;
  border: 1px solid var(--line);
  border-radius: 8px;
  padding: 8px 10px;
}
.actions { margin: 8px 0 0; padding-left: 18px; }
.actions li { margin-bottom: 6px; }
.hash {
  font-family: ui-monospace, Menlo, monospace;
  font-size: 12px;
  color: var(--muted);
  word-break: break-all;
}
)CSS";

}  // namespace

std::string buildHtmlReport(const AuditBundle& bundle) {
  std::ostringstream oss;
  oss << "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n";
  oss << "<meta charset=\"utf-8\">\n";
  oss << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  oss << "<title>遮挡查询统计 · 来源检查页</title>\n";
  oss << "<style>" << kPageStyle << "</style>\n</head>\n<body>\n";
  oss << "<header>\n";
  oss << "<h1>遮挡查询统计 · 来源检查页</h1>\n";
  oss << "<div class=\"sub\">运行编号 <code>"
      << htmlEscape(bundle.runId) << "</code> · 生成时间(UTC) "
      << htmlEscape(bundle.createdAtUtc) << "</div>\n";
  oss << "<div class=\"meta\">\n";
  oss << "<div><span>设备</span><b>"
      << htmlEscape(bundle.environment.deviceName) << " / Vulkan "
      << htmlEscape(apiVersionString(bundle.environment.apiVersion))
      << "</b></div>\n";
  oss << "<div><span>GLFW</span><b>"
      << htmlEscape(bundle.environment.glfwVersion)
      << "（NO_API 隐藏窗口）</b></div>\n";
  oss << "<div><span>原始输入哈希</span><b class=\"hash\">"
      << htmlEscape(bundle.inputSha256) << "</b></div>\n";
  oss << "<div><span>账本哈希</span><b class=\"hash\">"
      << htmlEscape(bundle.ledgerSha256) << "</b></div>\n";
  oss << "</div>\n</header>\n<main>\n";

  oss << "<div class=\"card\">\n<h2>核查范围与可追溯性说明</h2>\n";
  oss << "<p class=\"sub\">每个场景均从“原始输入 → 处理步骤 → 原始 Vulkan 返回值 → 自动结论 → 人工确认”单向串联。"
         "自动结论只依据 <code>vkGetQueryPoolResults</code> 的返回码与 availability 位生成；"
         "人工确认区保留独立判断位，不与自动结论合并。哈希基于原始输入账本（raw_input_ledger.txt）计算。</p>\n";
  oss << "</div>\n";

  for (const auto& rec : bundle.scenarios) {
    oss << "<div class=\"card " << verdictClass(rec.verdict) << "\">\n";
    oss << "<div class=\"section-title\">\n";
    oss << "<span class=\"badge\">" << verdictLabel(rec.verdict)
        << "</span>\n";
    oss << "<h3>" << htmlEscape(rec.title) << "</h3>\n";
    oss << "<span class=\"sub\">" << htmlEscape(rec.id)
        << " / kind=" << toString(rec.kind) << "</span>\n";
    oss << "</div>\n";

    oss << "<h3>① 原始输入</h3>\n<table>\n";
    oss << "<tr><th>查询槽</th><th>对象</th><th>用途说明</th>"
           "<th>begin 已录制</th><th>end 已录制</th><th>已提交</th></tr>\n";
    oss << "<tr><td>" << rec.input.slot << "</td><td>"
        << htmlEscape(rec.input.label) << "</td><td>"
        << htmlEscape(rec.input.expectedUse) << "</td><td>"
        << (rec.input.beginRecorded ? "是" : "否") << "</td><td>"
        << (rec.input.endRecorded ? "是" : "否") << "</td><td>"
        << (rec.input.submitted ? "是" : "否") << "</td></tr>\n";
    oss << "</table>\n";

    oss << "<h3 style=\"margin-top:14px\">② 处理步骤（含证据编号）</h3>\n<ol class=\"steps\">\n";
    for (const auto& step : rec.steps) {
      oss << "<li><b>" << step.order << ". "
          << htmlEscape(step.stage) << "</b> — "
          << htmlEscape(step.detail)
          << " <span class=\"ref\">["
          << htmlEscape(step.evidenceRef) << "]</span></li>\n";
    }
    oss << "</ol>\n";

    oss << "<h3>③ 关键证据：原始 Vulkan 返回</h3>\n<div class=\"evidence-row\">\n";
    oss << "<div><span class=\"sub\">vkResult</span><br><code>"
        << htmlEscape(vkResultNameLocal(
               static_cast<VkResult>(rec.result.vkResult)))
        << "</code></div>\n";
    oss << "<div><span class=\"sub\">sampleCount</span><br><code>"
        << rec.result.sampleCount << "</code></div>\n";
    oss << "<div><span class=\"sub\">availability</span><br><code>"
        << rec.result.availability << "</code></div>\n";
    oss << "<div><span class=\"sub\">resultFlags</span><br><code>0x"
        << std::hex << rec.result.resultFlags << std::dec
        << "</code></div>\n";
    oss << "</div>\n";

    oss << "<div class=\"auto-zone\">\n<h3>④ 自动结论（机器生成）</h3>\n";
    oss << "<p><span class=\"badge\">" << verdictLabel(rec.verdict)
        << "</span></p>\n";
    oss << "<p class=\"sub\">判定依据：" << htmlEscape(rec.verdictReason)
        << "</p>\n</div>\n";

    oss << "<div class=\"manual-zone\">\n<h3>⑤ 人工确认（独立分区，不覆盖自动结论）</h3>\n";
    oss << "<p>状态：<b>" << htmlEscape(rec.manualStatus) << "</b></p>\n";
    oss << "<p class=\"sub\">" << htmlEscape(rec.manualNote) << "</p>\n";
    oss << "</div>\n";

    if (!rec.followUpActions.empty()) {
      oss << "<h3 style=\"margin-top:14px\">⑥ 失败/不确定时的可执行后续操作</h3>\n";
      oss << "<ul class=\"actions\">\n";
      for (const auto& action : rec.followUpActions) {
        oss << "<li>" << htmlEscape(action) << "</li>\n";
      }
      oss << "</ul>\n";
    }
    oss << "</div>\n";
  }

  oss << "<div class=\"card\">\n<h2>证据归档</h2>\n";
  oss << "<p class=\"sub\">同目录下的 <code>raw_input_ledger.txt</code> 为原始输入账本，"
         "<code>audit_report.json</code> 为机器可读全量账本；二者与本页面的证据编号一一对应。</p>\n";
  oss << "<p class=\"hash\">input_sha256=" << htmlEscape(bundle.inputSha256)
      << "<br>ledger_sha256=" << htmlEscape(bundle.ledgerSha256)
      << "</p>\n</div>\n";
  oss << "</main>\n</body>\n</html>\n";
  return oss.str();
}

ReportPaths writeReports(const AuditBundle& bundle,
                         const std::string& outputDir) {
  ReportPaths paths;
  paths.rawInputPath = outputDir + "/raw_input_ledger.txt";
  paths.jsonPath = outputDir + "/audit_report.json";
  paths.htmlPath = outputDir + "/source_check.html";

  std::string ledger = buildRawInputLedger(bundle);
  std::ofstream raw(paths.rawInputPath, std::ios::binary);
  raw.write(ledger.data(), static_cast<std::streamsize>(ledger.size()));
  raw.close();

  std::string json = buildJsonReport(bundle);
  std::ofstream jsonFile(paths.jsonPath, std::ios::binary);
  jsonFile.write(json.data(), static_cast<std::streamsize>(json.size()));

  std::string html = buildHtmlReport(bundle);
  std::ofstream htmlFile(paths.htmlPath, std::ios::binary);
  htmlFile.write(html.data(), static_cast<std::streamsize>(html.size()));
  return paths;
}

}  // namespace audit
