#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pcd {

// Wall-clock helpers (UTC). Acceptance evidence is anchored to these.
std::string utc_now_iso();          // e.g. 2026-11-22T08:19:03.123Z
std::string utc_now_compact();      // e.g. 20261122T081903Z (dir-safe)
uint64_t now_unix_ms();

// Filesystem helpers.
bool file_exists(const std::string& path);
uint64_t file_size(const std::string& path);
void make_dirs(const std::string& path);
void copy_file(const std::string& src, const std::string& dst);
void copy_dir(const std::string& src, const std::string& dst);
void make_readonly(const std::string& path);
std::vector<uint8_t> read_file(const std::string& path);
void write_file(const std::string& path, const void* data, size_t size);
void write_text(const std::string& path, const std::string& text);
void append_text(const std::string& path, const std::string& text);

// Misc.
std::string hex_encode(const uint8_t* data, size_t n);
std::string json_escape(const std::string& s);
[[noreturn]] void die(const std::string& msg);

} // namespace pcd
