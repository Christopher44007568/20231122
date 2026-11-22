#pragma once
#include <fstream>
#include <string>
#include <vector>

// Append-only JSONL journal: one line per experiment step recording
// input, state before/after, and result. Journals are the replay source.
class Journal {
public:
    bool open(const std::string& path);
    // Fields are appended verbatim in fixed key order so the replay parser
    // can rely on stable "key": positions. Callers pass pre-built fragments.
    void writeLine(const std::string& jsonLine);
    void close();
    const std::string& path() const { return path_; }
private:
    std::ofstream out_;
    std::string path_;
};

namespace journal {

std::string escape(const std::string& s);
std::string kv(const std::string& key, const std::string& value);       // "k":"v"
std::string kv(const std::string& key, const char* value);              // "k":"v"
std::string kv(const std::string& key, long long value);                // "k":n
std::string kv(const std::string& key, unsigned long long value);       // "k":n
std::string kv(const std::string& key, bool value);                     // "k":bool
std::string join(const std::vector<std::string>& fields, const char* sep = ",");

// Minimal extractors used by replay; rely on the fixed emitter key order.
bool extractString(const std::string& line, const std::string& key, std::string& out);
bool extractInt(const std::string& line, const std::string& key, long long& out);
std::vector<std::string> readLines(const std::string& path);

} // namespace journal
