#include "report.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace {

const char* severityName(Severity s) {
  switch (s) {
    case Severity::Info: return "INFO";
    case Severity::Warning: return "WARNING";
    case Severity::Error: return "ERROR";
  }
  return "?";
}

std::string jsonEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      default: out += c;
    }
  }
  return out;
}

}  // namespace

bool Report::hasErrors() const {
  for (const auto& f : autoFindings)
    if (f.severity == Severity::Error) return true;
  return false;
}

bool Report::hasWarnings() const {
  for (const auto& f : autoFindings)
    if (f.severity == Severity::Warning) return true;
  return false;
}

void writeReport(const Report& r, const std::string& dir) {
  {
    std::ofstream md(dir + "/report.md", std::ios::trunc);
    if (!md) throw std::runtime_error("cannot write report.md in " + dir);
    md << "# " << r.title << "\n\n";
    md << "## 上下文\n\n";
    for (const auto& l : r.contextLines) md << "- " << l << "\n";
    md << "\n## 自动结论\n\n";
    if (r.autoFindings.empty()) md << "- （无）\n";
    for (const auto& f : r.autoFindings)
      md << "- [" << severityName(f.severity) << "] (" << f.category << ") " << f.message << "\n";
    md << "\n## 人工确认\n\n";
    if (r.manualChecks.empty()) md << "- （无）\n";
    for (size_t i = 0; i < r.manualChecks.size(); ++i)
      md << "- [ ] " << r.manualChecks[i] << "\n";
    md << "\n## 后续操作\n\n";
    if (r.nextActions.empty()) md << "- （无）\n";
    for (const auto& a : r.nextActions) md << "- " << a << "\n";
    md << "\n## 证据清单\n\n";
    md << "| 文件 | 作用 | FNV-1a |\n|---|---|---|\n";
    for (const auto& e : r.evidence) {
      char hash[32];
      std::snprintf(hash, sizeof(hash), "%016llx", (unsigned long long)e.fnv1a);
      md << "| " << e.path << " | " << e.role << " | " << hash << " |\n";
    }
  }
  {
    std::ofstream js(dir + "/report.json", std::ios::trunc);
    if (!js) throw std::runtime_error("cannot write report.json in " + dir);
    js << "{\n  \"title\": \"" << jsonEscape(r.title) << "\",\n  \"context\": [";
    for (size_t i = 0; i < r.contextLines.size(); ++i)
      js << (i ? ", " : "") << "\"" << jsonEscape(r.contextLines[i]) << "\"";
    js << "],\n  \"automatedConclusions\": [";
    for (size_t i = 0; i < r.autoFindings.size(); ++i) {
      const auto& f = r.autoFindings[i];
      js << (i ? ", " : "") << "{\"severity\": \"" << severityName(f.severity)
         << "\", \"category\": \"" << jsonEscape(f.category) << "\", \"message\": \""
         << jsonEscape(f.message) << "\"}";
    }
    js << "],\n  \"manualConfirmations\": [";
    for (size_t i = 0; i < r.manualChecks.size(); ++i)
      js << (i ? ", " : "") << "\"" << jsonEscape(r.manualChecks[i]) << "\"";
    js << "],\n  \"nextActions\": [";
    for (size_t i = 0; i < r.nextActions.size(); ++i)
      js << (i ? ", " : "") << "\"" << jsonEscape(r.nextActions[i]) << "\"";
    js << "],\n  \"evidence\": [";
    for (size_t i = 0; i < r.evidence.size(); ++i) {
      const auto& e = r.evidence[i];
      char hash[32];
      std::snprintf(hash, sizeof(hash), "%016llx", (unsigned long long)e.fnv1a);
      js << (i ? ", " : "") << "{\"path\": \"" << jsonEscape(e.path) << "\", \"role\": \""
         << jsonEscape(e.role) << "\", \"fnv1a\": \"" << hash << "\"}";
    }
    js << "]\n}\n";
  }
}
