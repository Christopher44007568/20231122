#pragma once
#include <ctime>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace util {

inline uint64_t fnv1a64(const std::string& data, uint64_t seed = 1469598103934665603ULL) {
    uint64_t hash = seed;
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline std::string hex16(uint64_t value) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
    return buf;
}

inline std::string hex_dump(const std::string& data, size_t max_bytes = 96) {
    std::string out;
    size_t n = data.size() < max_bytes ? data.size() : max_bytes;
    char buf[8];
    for (size_t i = 0; i < n; i++) {
        std::snprintf(buf, sizeof(buf), "%02x", static_cast<unsigned char>(data[i]));
        out += buf;
        if (i % 2 == 1) out += ' ';
    }
    if (data.size() > n) out += "...";
    return out;
}

inline std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Minimal flat JSON object: string or number values only.
using JsonObj = std::map<std::string, std::string>;

inline std::string json_build(const JsonObj& obj) {
    std::string out = "{";
    bool first = true;
    for (const auto& kv : obj) {
        if (!first) out += ",";
        first = false;
        out += "\"" + json_escape(kv.first) + "\":";
        const std::string& v = kv.second;
        bool numeric = !v.empty();
        size_t dots = 0;
        for (size_t k = 0; k < v.size() && numeric; k++) {
            char c = v[k];
            if (c == '-' && k == 0) continue;
            if (c == '.') { numeric = ++dots <= 1; continue; }
            if (c < '0' || c > '9') numeric = false;
        }
        if (numeric) {
            out += v;
        } else {
            out += "\"" + json_escape(v) + "\"";
        }
    }
    out += "}";
    return out;
}

// Parses a flat JSON object produced by json_build. Returns false on any
// structural problem (this is how a truncated line is detected).
inline bool json_parse(const std::string& line, JsonObj& out) {
    size_t i = 0;
    auto skip_ws = [&]() { while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++; };
    auto parse_string = [&](std::string& s) -> bool {
        if (i >= line.size() || line[i] != '"') return false;
        i++;
        s.clear();
        while (i < line.size() && line[i] != '"') {
            if (line[i] == '\\') {
                i++;
                if (i >= line.size()) return false;
                switch (line[i]) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case 'u':
                        if (i + 4 >= line.size()) return false;
                        i += 4;
                        s += '?';
                        break;
                    default: s += line[i];
                }
            } else {
                s += line[i];
            }
            i++;
        }
        if (i >= line.size()) return false;
        i++;
        return true;
    };
    skip_ws();
    if (i >= line.size() || line[i] != '{') return false;
    i++;
    skip_ws();
    if (i < line.size() && line[i] == '}') { i++; return true; }
    while (true) {
        skip_ws();
        std::string key;
        if (!parse_string(key)) return false;
        skip_ws();
        if (i >= line.size() || line[i] != ':') return false;
        i++;
        skip_ws();
        std::string value;
        if (i < line.size() && line[i] == '"') {
            if (!parse_string(value)) return false;
        } else {
            size_t start = i;
            while (i < line.size() && line[i] != ',' && line[i] != '}') i++;
            value = line.substr(start, i - start);
            if (value.empty()) return false;
        }
        out[key] = value;
        skip_ws();
        if (i >= line.size()) return false;
        if (line[i] == ',') { i++; continue; }
        if (line[i] == '}') { i++; break; }
        return false;
    }
    skip_ws();
    return i == line.size();
}

inline std::string now_iso8601() {
    time_t t = time(nullptr);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

}  // namespace util
