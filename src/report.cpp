#include "report.h"

#include <cstdio>
#include <vulkan/vulkan.h>

namespace report {

static std::string esc(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

static const char* badge_class(analyze::Verdict v) {
    switch (v) {
        case analyze::Verdict::OK: return "ok";
        case analyze::Verdict::ERROR: return "err";
        case analyze::Verdict::UNDECIDABLE: return "und";
    }
    return "";
}

static const char* kCss = R"CSS(
body{font-family:system-ui,sans-serif;margin:2rem auto;max-width:1100px;color:#1c1e21}
h1{font-size:1.4rem}h2{font-size:1.1rem;margin-top:2rem;border-bottom:2px solid #ddd;padding-bottom:.2rem}
table{border-collapse:collapse;width:100%;font-size:.85rem}
th,td{border:1px solid #ccc;padding:.35rem .5rem;text-align:left;vertical-align:top}
th{background:#f2f3f5}
.badge{display:inline-block;padding:.1rem .5rem;border-radius:.7rem;font-weight:600;font-size:.75rem;color:#fff}
.ok{background:#2e7d32}.err{background:#c62828}.und{background:#ef6c00}
.banner{border:2px solid #c62828;background:#fdecea;padding:.8rem 1rem;border-radius:.4rem;margin:1rem 0}
.banner.none{border-color:#2e7d32;background:#e8f5e9}
pre{background:#f6f8fa;border:1px solid #ddd;padding:.5rem;overflow-x:auto;font-size:.78rem;white-space:pre-wrap;word-break:break-all}
.meta{color:#555;font-size:.85rem}
a{color:#0b5fff}
.anchor:target{background:#fff3cd}
code{background:#f0f0f0;padding:0 .25rem;border-radius:.2rem}
)CSS";

std::string render_html(const Data& d) {
    std::string h;
    h += "<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">";
    h += "<title>GPU 时间戳采样 · 来源检查页</title><style>";
    h += kCss;
    h += "</style></head><body>";
    h += "<h1>GPU 时间戳采样 · 来源检查页</h1>";
    h += "<p class=\"meta\">run_id <code>" + esc(d.run_id) + "</code> · 生成于 " +
         esc(d.generated_at) + " · 设备 <code>" + esc(d.device.name) + "</code> · GLFW 平台 " +
         (d.device.glfw_null_platform ? "null(无显示)" : "原生窗口") + "</p>";

    // --- First error banner: the headline is the first broken state, not a score.
    h += "<h2>第一个错误状态</h2>";
    if (d.analysis.first_error_index >= 0) {
        const auto& c = d.analysis.conclusions[d.analysis.first_error_index];
        h += "<div class=\"banner\">首个错误状态:结论 <a href=\"#concl-" +
             std::to_string(c.id) + "\">C" + std::to_string(c.id) + "</a> · seq " +
             std::to_string(c.seq) + " · 字节偏移 " + std::to_string(c.byte_offset) + " · " +
             esc(c.subject) + "<br>" + esc(c.detail) + "</div>";
    } else {
        h += "<div class=\"banner none\">未发现错误状态。</div>";
    }
    char counts[160];
    std::snprintf(counts, sizeof(counts), "统计(仅供参考,不作为结论):OK %zu · ERROR %zu · UNDECIDABLE %zu",
                  d.analysis.count_ok, d.analysis.count_error, d.analysis.count_undecidable);
    h += "<p class=\"meta\">" + std::string(counts) + "</p>";

    // --- Conclusions: each row links back to raw input and its step.
    h += "<h2>结论列表(每条可回溯)</h2><table><tr>"
         "<th>结论</th><th>对象</th><th>判定</th><th>依据</th>"
         "<th>原始输入</th><th>处理步骤</th></tr>";
    for (const auto& c : d.analysis.conclusions) {
        h += "<tr class=\"anchor\" id=\"concl-" + std::to_string(c.id) + "\">";
        h += "<td>C" + std::to_string(c.id) + "</td>";
        h += "<td>" + esc(c.subject) + "<br><span class=\"meta\">seq " +
             std::to_string(c.seq) + " · 偏移 " + std::to_string(c.byte_offset) + "</span></td>";
        h += std::string("<td><span class=\"badge ") + badge_class(c.verdict) + "\">" +
             analyze::verdict_name(c.verdict) + "</span></td>";
        h += "<td>" + esc(c.detail) + "</td>";
        h += "<td><a href=\"#raw-" + std::to_string(c.id) + "\">原始字节</a></td>";
        h += "<td><a href=\"#step-" + esc(c.step_id) + "\">" + esc(c.step_id) + "</a></td>";
        h += "</tr>";
    }
    h += "</table>";

    // --- Raw input excerpts: verbatim bytes each conclusion rests on.
    h += "<h2>原始输入(逐条留证)</h2>";
    for (const auto& c : d.analysis.conclusions) {
        h += "<div class=\"anchor\" id=\"raw-" + std::to_string(c.id) + "\">";
        h += "<p class=\"meta\">C" + std::to_string(c.id) + " · 偏移 " +
             std::to_string(c.byte_offset) + " · " + std::to_string(c.raw_excerpt.size()) +
             " 字节</p><pre>" + esc(c.raw_excerpt) + "</pre></div>";
    }

    // --- Processing steps.
    h += "<h2>处理步骤</h2><table><tr><th>步骤</th><th>名称</th><th>输入 → 输出</th></tr>";
    for (const auto& s : d.steps) {
        h += "<tr class=\"anchor\" id=\"step-" + esc(s.id) + "\"><td>" + esc(s.id) +
             "</td><td>" + esc(s.name) + "</td><td>" + esc(s.detail) + "</td></tr>";
    }
    h += "</table>";

    // --- Evidence.
    h += "<h2>关键证据</h2>";
    h += "<h3>环境证据</h3><pre>设备: " + esc(d.device.name) +
         "\nVulkan API: " + std::to_string(VK_VERSION_MAJOR(d.device.api_version)) + "." +
         std::to_string(VK_VERSION_MINOR(d.device.api_version)) +
         "\ntimestampPeriod: " + std::to_string(d.device.timestamp_period_ns) + " ns" +
         "\ntimestampValidBits: " + std::to_string(d.device.timestamp_valid_bits) +
         "\n预算: " + std::to_string(d.budget_ns) + " ns/帧</pre>";
    h += "<h3>日志完整性</h3><pre>日志文件: " + esc(d.log_path) +
         "\n最终内容 FNV-1a-64: " + esc(d.log_sha) + "</pre>";
    if (d.truncation.present) {
        h += "<h3>截断事件(崩溃现场)</h3><pre>截断偏移: " +
             std::to_string(d.truncation.byte_offset) +
             "\n原因: " + esc(d.truncation.reason) +
             "\n打捞字节已保存: " + esc(d.truncation.evidence_file) +
             "\n打捞内容(hex): " + esc(d.truncation_salvaged_hex) + "</pre>";
    }
    h += "<h3>哈希链</h3><table><tr><th>seq</th><th>kind</th><th>origin</th>"
         "<th>prev</th><th>hash</th><th>偏移</th></tr>";
    for (const auto& r : d.records) {
        auto get = [&](const char* k) -> std::string {
            auto it = r.fields.find(k);
            return it == r.fields.end() ? "" : it->second;
        };
        h += "<tr><td>" + esc(get("seq")) + "</td><td>" + esc(get("kind")) +
             "</td><td>" + esc(get("origin")) + "</td><td><code>" + esc(get("prev")) +
             "</code></td><td><code>" + esc(get("hash")) + "</code></td><td>" +
             std::to_string(r.byte_offset) + "</td></tr>";
    }
    h += "</table>";
    h += "</body></html>";
    return h;
}

}  // namespace report
