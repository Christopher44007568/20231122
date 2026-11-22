#include "journal.hpp"

#include <cstdlib>

namespace inst {
namespace {

size_t findValueStart(const std::string& line, const std::string& key) {
    const std::string needle = jsonString(key) + ':';
    const size_t keyPos = line.find(needle);
    if (keyPos == std::string::npos) return std::string::npos;
    size_t pos = keyPos + needle.size();
    while (pos < line.size() &&
           (line[pos] == ' ' || line[pos] == '\t' || line[pos] == '\n')) {
        ++pos;
    }
    return pos;
}

std::string decodeJsonString(const std::string& raw) {
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 >= raw.size()) {
            out.push_back(raw[i]);
            continue;
        }
        const char next = raw[++i];
        switch (next) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
                if (i + 4 < raw.size()) i += 4;
                out.push_back('?');
                break;
            default: out.push_back(next); break;
        }
    }
    return out;
}

} // namespace

Journal::Journal(const fs::path& path) : out_(path) {
    if (!out_) throw std::runtime_error("cannot open journal " + path.string());
}

void Journal::writeStep(const std::string& step,
                        const std::string& input,
                        const std::string& stateBefore,
                        const std::string& result,
                        const std::string& stateAfter,
                        bool ok,
                        const std::string& error,
                        const std::string& check) {
    out_ << '{'
         << field("seq", sequence_++) << ','
         << field("timestamp", jsonString(nowIso8601())) << ','
         << field("step", jsonString(step)) << ','
         << field("input", input.empty() ? "{}" : input) << ','
         << field("state_before", stateBefore.empty() ? "{}" : stateBefore) << ','
         << field("result", result.empty() ? "{}" : result) << ','
         << field("state_after", stateAfter.empty() ? "{}" : stateAfter) << ','
         << field("ok", ok) << ','
         << field("error", jsonString(error)) << ','
         << field("check", check.empty() ? "{}" : check)
         << "}\n";
    out_.flush();
}

std::vector<std::string> readJournalLines(const fs::path& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open journal " + path.string());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

bool extractString(const std::string& line, const std::string& key,
                   std::string& value) {
    const size_t start = findValueStart(line, key);
    if (start == std::string::npos || start >= line.size() || line[start] != '"') {
        return false;
    }
    std::string raw;
    bool escaped = false;
    for (size_t i = start + 1; i < line.size(); ++i) {
        const char ch = line[i];
        if (escaped) {
            raw.push_back('\\');
            raw.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            value = decodeJsonString(raw);
            return true;
        }
        raw.push_back(ch);
    }
    return false;
}

bool extractInt(const std::string& line, const std::string& key,
                int64_t& value) {
    const size_t start = findValueStart(line, key);
    if (start == std::string::npos) return false;
    const char* begin = line.c_str() + start;
    char* end = nullptr;
    const long long parsed = std::strtoll(begin, &end, 10);
    if (end == begin) return false;
    value = parsed;
    return true;
}

bool extractBool(const std::string& line, const std::string& key,
                 bool& value) {
    const size_t start = findValueStart(line, key);
    if (start == std::string::npos) return false;
    if (line.compare(start, 4, "true") == 0) {
        value = true;
        return true;
    }
    if (line.compare(start, 5, "false") == 0) {
        value = false;
        return true;
    }
    return false;
}

} // namespace inst
