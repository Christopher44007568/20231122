#pragma once

#include <string>
#include <vector>

namespace report {

enum class Verdict { Supported, Unsupported, Unknown, Indeterminate, Error };

std::string verdictLabel(Verdict v);
std::string verdictClass(Verdict v);

struct Evidence {
    std::string id;
    std::string kind;    // e.g. "raw-input", "derived"
    std::string content; // raw captured text
};

struct Step {
    std::string id;
    std::string text;
};

struct Conclusion {
    std::string objectId;
    std::string title;
    Verdict verdict = Verdict::Unknown;
    std::string summary;
    std::vector<std::string> evidenceIds;
    std::vector<Step> steps;
};

class Report {
public:
    std::string addEvidence(const std::string& kind, const std::string& content);
    void addConclusion(Conclusion c);
    void addCleanupNote(const std::string& note);

    const std::vector<Evidence>& evidences() const { return evidences_; }

    void writeHtml(const std::string& path) const;
    void writeJson(const std::string& path) const;

private:
    std::vector<Evidence> evidences_;
    std::vector<Conclusion> conclusions_;
    std::vector<std::string> cleanupNotes_;
    int evidenceCounter_ = 0;
};

std::string htmlEscape(const std::string& s);
std::string jsonEscape(const std::string& s);

} // namespace report
