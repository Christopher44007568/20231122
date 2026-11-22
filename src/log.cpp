#include "log.h"

#include <cerrno>
#include <cstring>

namespace tslog {

std::string RecordLog::chain_hash(const std::string& prev_hash, const util::JsonObj& fields) {
    // Canonical preimage: prev hash + canonical JSON of fields (without the
    // chain fields themselves).
    util::JsonObj body = fields;
    body.erase("prev");
    body.erase("hash");
    return util::hex16(util::fnv1a64(prev_hash + "|" + util::json_build(body)));
}

bool RecordLog::open_append(const std::string& path) {
    path_ = path;
    // Recover chain state if the file already has good records.
    ScanResult res = scan(path);
    head_hash_ = res.last_good_hash;
    next_seq_ = res.next_seq;
    offset_ = 0;
    FILE* probe = std::fopen(path.c_str(), "rb");
    if (probe) {
        std::fseek(probe, 0, SEEK_END);
        offset_ = static_cast<uint64_t>(std::ftell(probe));
        std::fclose(probe);
    }
    file_ = std::fopen(path.c_str(), "ab");
    return file_ != nullptr;
}

uint64_t RecordLog::append(util::JsonObj fields) {
    fields["seq"] = std::to_string(next_seq_);
    fields["prev"] = head_hash_;
    fields["hash"] = chain_hash(head_hash_, fields);
    std::string line = util::json_build(fields) + "\n";
    uint64_t at = offset_;
    std::fwrite(line.data(), 1, line.size(), file_);
    std::fflush(file_);
    head_hash_ = fields["hash"];
    next_seq_++;
    offset_ += line.size();
    return at;
}

uint64_t RecordLog::append_partial(util::JsonObj fields, size_t bytes) {
    fields["seq"] = std::to_string(next_seq_);
    fields["prev"] = head_hash_;
    fields["hash"] = chain_hash(head_hash_, fields);
    std::string line = util::json_build(fields) + "\n";
    if (bytes > line.size()) bytes = line.size();
    uint64_t at = offset_;
    std::fwrite(line.data(), 1, bytes, file_);
    std::fflush(file_);
    // Chain state intentionally NOT advanced: the record never committed.
    offset_ += bytes;
    return at;
}

void RecordLog::close() {
    if (file_) std::fclose(file_);
    file_ = nullptr;
}

ScanResult RecordLog::scan(const std::string& path) {
    ScanResult res;
    res.last_good_hash = kGenesis;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return res;  // no log yet: genesis state
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::string data(static_cast<size_t>(size), '\0');
    if (size > 0) std::fread(&data[0], 1, static_cast<size_t>(size), f);
    std::fclose(f);

    uint64_t offset = 0;
    uint64_t expect_seq = 0;
    std::string prev = kGenesis;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t nl = data.find('\n', pos);
        bool has_newline = nl != std::string::npos;
        size_t end = has_newline ? nl : data.size();
        std::string line = data.substr(pos, end - pos);

        util::JsonObj fields;
        std::string reason;
        if (!has_newline) {
            reason = "line not terminated (writer stopped mid-record)";
        } else if (!util::json_parse(line, fields)) {
            reason = "line is not a well-formed record";
        } else if (fields.count("seq") == 0 || fields.count("prev") == 0 ||
                   fields.count("hash") == 0) {
            reason = "record misses chain fields";
        } else if (fields["prev"] != prev) {
            reason = "prev hash does not match chain head";
        } else if (fields["hash"] != chain_hash(prev, fields)) {
            reason = "record hash mismatch";
        } else if (std::stoull(fields["seq"]) != expect_seq) {
            reason = "seq gap or reorder";
        }

        if (!reason.empty()) {
            res.truncated = true;
            res.bad_offset = offset;
            res.bad_bytes = data.substr(pos);
            res.bad_reason = reason;
            break;
        }

        Record rec;
        rec.fields = fields;
        rec.raw_line = line;
        rec.byte_offset = offset;
        rec.seq = expect_seq;
        rec.kind = fields.count("kind") ? fields["kind"] : "?";
        res.good.push_back(rec);

        prev = fields["hash"];
        res.last_good_hash = prev;
        expect_seq++;
        offset = end + 1;
        pos = end + 1;
    }
    res.next_seq = expect_seq;
    return res;
}

}  // namespace tslog
