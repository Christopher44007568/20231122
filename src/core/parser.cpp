#include "parser.hpp"

#include <cctype>
#include <istream>
#include <string>

namespace gcdump {

std::string trim(std::string_view text) {
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

ParseResult parseDump(std::istream& in) {
    ParseResult result;
    std::string raw;
    std::size_t lineNo = 0;

    while (std::getline(in, raw)) {
        ++lineNo;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        result.lines.push_back(SourceLine{lineNo, raw});

        std::string content = trim(raw);
        if (content.empty() || content[0] == '#') continue;

        std::size_t colon = content.find(':');
        if (colon == std::string::npos) {
            result.issues.push_back(
                ParseIssue{Severity::Error, lineNo, "E1001",
                           "missing ':' separator; line cannot be parsed"});
            continue;
        }

        std::string key = trim(content.substr(0, colon));
        std::string value = trim(content.substr(colon + 1));
        if (key.empty()) {
            result.issues.push_back(
                ParseIssue{Severity::Error, lineNo, "E1002",
                           "empty key before ':'"});
            continue;
        }
        result.fields.push_back(FieldRecord{key, value, lineNo});
    }

    if (lineNo == 0) {
        result.issues.push_back(
            ParseIssue{Severity::Error, 0, "E1000", "input is empty"});
    }
    return result;
}

} // namespace gcdump
