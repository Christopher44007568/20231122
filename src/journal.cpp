#include "journal.h"
#include <sstream>

bool Journal::open(const std::string& path) {
    path_ = path;
    out_.open(path, std::ios::out | std::ios::trunc);
    return out_.good();
}

void Journal::writeLine(const std::string& jsonLine) {
    out_ << jsonLine << '\n';
    out_.flush();
}

void Journal::close() {
    if (out_.is_open()) out_.close();
}

namespace journal {

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string kv(const std::string& key, const std::string& value) {
    return "\"" + key + "\":\"" + escape(value) + "\"";
}

std::string kv(const std::string& key, const char* value) {
    return kv(key, std::string(value));
}

std::string kv(const std::string& key, long long value) {
    return "\"" + key + "\":" + std::to_string(value);
}

std::string kv(const std::string& key, unsigned long long value) {
    return "\"" + key + "\":" + std::to_string(value);
}

std::string kv(const std::string& key, bool value) {
    return std::string("\"") + key + "\":" + (value ? "true" : "false");
}

std::string join(const std::vector<std::string>& fields, const char* sep) {
    std::string out;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) out += sep;
        out += fields[i];
    }
    return out;
}

bool extractString(const std::string& line, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\":\"";
    const size_t pos = line.find(needle);
    if (pos == std::string::npos) return false;
    const size_t start = pos + needle.size();
    const size_t end = line.find('"', start);
    if (end == std::string::npos) return false;
    out = line.substr(start, end - start);
    return true;
}

bool extractInt(const std::string& line, const std::string& key, long long& out) {
    const std::string needle = "\"" + key + "\":";
    const size_t pos = line.find(needle);
    if (pos == std::string::npos) return false;
    const size_t start = pos + needle.size();
    try {
        out = std::stoll(line.substr(start));
    } catch (...) {
        return false;
    }
    return true;
}

std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

} // namespace journal
