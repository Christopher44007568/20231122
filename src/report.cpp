#include "report.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace report {

std::string verdictLabel(Verdict v) {
    switch (v) {
        case Verdict::Supported:     return "成立";
        case Verdict::Unsupported:   return "不成立";
        case Verdict::Unknown:       return "未知";
        case Verdict::Indeterminate: return "无法判断";
        case Verdict::Error:         return "处理异常";
    }
    return "?";
}

std::string verdictClass(Verdict v) {
    switch (v) {
        case Verdict::Supported:     return "ok";
        case Verdict::Unsupported:   return "no";
        case Verdict::Unknown:       return "unk";
        case Verdict::Indeterminate: return "ind";
        case Verdict::Error:         return "err";
    }
    return "unk";
}

std::string htmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string Report::addEvidence(const std::string& kind, const std::string& content) {
    Evidence e;
    e.id = "E" + std::to_string(++evidenceCounter_);
    e.kind = kind;
    e.content = content;
    evidences_.push_back(e);
    return e.id;
}

void Report::addConclusion(Conclusion c) { conclusions_.push_back(std::move(c)); }

void Report::addCleanupNote(const std::string& note) { cleanupNotes_.push_back(note); }

void Report::writeHtml(const std::string& path) const {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path);

    f << "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n<meta charset=\"utf-8\">\n"
      << "<title>抗锯齿历史验证 · 来源检查页</title>\n<style>\n"
      << "body{font-family:system-ui,sans-serif;margin:2rem;background:#fafafa;color:#222}"
      << "h1{font-size:1.4rem}h2{font-size:1.1rem;margin-top:2rem}"
      << ".card{background:#fff;border:1px solid #ddd;border-radius:8px;padding:1rem;margin:.8rem 0}"
      << ".badge{display:inline-block;padding:.1rem .6rem;border-radius:1rem;font-size:.85rem;color:#fff}"
      << ".ok{background:#2e7d32}.no{background:#c62828}.unk{background:#757575}"
      << ".ind{background:#ef6c00}.err{background:#6a1b9a}"
      << "pre{background:#f4f4f4;border:1px solid #e0e0e0;border-radius:6px;padding:.6rem;"
      << "overflow-x:auto;font-size:.85rem}"
      << "a{color:#1565c0}ol.steps li{margin:.2rem 0}.meta{color:#666;font-size:.85rem}"
      << "</style>\n</head>\n<body>\n"
      << "<h1>抗锯齿历史验证 · 来源检查页</h1>\n"
      << "<p class=\"meta\">每条结论均可回溯到原始输入（证据）与处理步骤。</p>\n";

    f << "<h2>结论</h2>\n";
    for (const auto& c : conclusions_) {
        f << "<div class=\"card\" id=\"obj-" << htmlEscape(c.objectId) << "\">\n"
          << "<h3>" << htmlEscape(c.title) << " <span class=\"badge "
          << verdictClass(c.verdict) << "\">" << verdictLabel(c.verdict)
          << "</span></h3>\n"
          << "<p>" << htmlEscape(c.summary) << "</p>\n"
          << "<p class=\"meta\">来源证据：";
        for (size_t i = 0; i < c.evidenceIds.size(); ++i) {
            if (i) f << "、";
            f << "<a href=\"#ev-" << c.evidenceIds[i] << "\">" << c.evidenceIds[i] << "</a>";
        }
        f << "</p>\n<ol class=\"steps\">\n";
        for (const auto& s : c.steps) {
            f << "<li id=\"step-" << s.id << "\">[" << s.id << "] "
              << htmlEscape(s.text) << "</li>\n";
        }
        f << "</ol>\n</div>\n";
    }

    f << "<h2>原始输入与证据</h2>\n";
    for (const auto& e : evidences_) {
        f << "<div class=\"card\" id=\"ev-" << e.id << "\">\n"
          << "<h3>" << e.id << " <span class=\"meta\">(" << htmlEscape(e.kind)
          << ")</span></h3>\n<pre>" << htmlEscape(e.content) << "</pre>\n</div>\n";
    }

    f << "<h2>临时状态清理</h2>\n<div class=\"card\"><ul>\n";
    for (const auto& n : cleanupNotes_) {
        f << "<li>" << htmlEscape(n) << "</li>\n";
    }
    f << "</ul></div>\n</body>\n</html>\n";
}

void Report::writeJson(const std::string& path) const {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path);
    f << "{\n  \"conclusions\": [\n";
    for (size_t i = 0; i < conclusions_.size(); ++i) {
        const auto& c = conclusions_[i];
        f << "    {\"object\": \"" << jsonEscape(c.objectId)
          << "\", \"verdict\": \"" << verdictLabel(c.verdict)
          << "\", \"summary\": \"" << jsonEscape(c.summary) << "\", \"evidence\": [";
        for (size_t j = 0; j < c.evidenceIds.size(); ++j) {
            if (j) f << ", ";
            f << "\"" << c.evidenceIds[j] << "\"";
        }
        f << "], \"steps\": [";
        for (size_t j = 0; j < c.steps.size(); ++j) {
            if (j) f << ", ";
            f << "{\"id\": \"" << c.steps[j].id << "\", \"text\": \""
              << jsonEscape(c.steps[j].text) << "\"}";
        }
        f << "]}" << (i + 1 < conclusions_.size() ? ",\n" : "\n");
    }
    f << "  ],\n  \"evidence\": {\n";
    for (size_t i = 0; i < evidences_.size(); ++i) {
        const auto& e = evidences_[i];
        f << "    \"" << e.id << "\": {\"kind\": \"" << jsonEscape(e.kind)
          << "\", \"content\": \"" << jsonEscape(e.content) << "\"}"
          << (i + 1 < evidences_.size() ? ",\n" : "\n");
    }
    f << "  }\n}\n";
}

} // namespace report
