#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "util.h"

namespace tslog {

// One validated record line plus its provenance anchors.
struct Record {
    util::JsonObj fields;
    std::string raw_line;
    uint64_t byte_offset = 0;  // offset of this line in the log file
    uint64_t seq = 0;
    std::string kind;
};

struct ScanResult {
    std::vector<Record> good;      // records before the first bad line
    bool truncated = false;        // a bad tail was found
    uint64_t bad_offset = 0;       // byte offset where the bad tail starts
    std::string bad_bytes;         // raw salvaged bytes of the bad tail
    std::string bad_reason;        // why the tail failed validation
    std::string last_good_hash;    // chain hash of the last good record
    uint64_t next_seq = 0;         // seq the next appended record must use
};

// Append-only JSONL log with an FNV-1a-64 hash chain across records.
class RecordLog {
  public:
    bool open_append(const std::string& path);
    // Appends fields as one canonical line; injects prev/hash chain fields.
    // Returns the byte offset of the written line.
    uint64_t append(util::JsonObj fields);
    // Writes only the first `bytes` of what append() would write, without a
    // newline: simulates a crash mid-write. Returns the offset written to.
    uint64_t append_partial(util::JsonObj fields, size_t bytes);
    void close();

    const std::string& path() const { return path_; }
    const std::string& head_hash() const { return head_hash_; }
    uint64_t next_seq() const { return next_seq_; }

    // Scans an existing log, validating structure + hash chain per line.
    static ScanResult scan(const std::string& path);

    static std::string chain_hash(const std::string& prev_hash, const util::JsonObj& fields);
    static constexpr const char* kGenesis = "genesis";

  private:
    std::string path_;
    std::string head_hash_ = kGenesis;
    uint64_t next_seq_ = 0;
    uint64_t offset_ = 0;
    FILE* file_ = nullptr;
};

}  // namespace tslog
