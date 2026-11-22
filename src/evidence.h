#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace vox {

// Minimal, dependency-free JSON string escaper.
std::string json_escape(const std::string& raw);

// One structured, append-only evidence record (JSON Lines).
// Every record carries a monotonic sequence number and a timestamp so the
// order of draw calls / state changes can be replayed during review.
class EvidenceLog {
public:
  explicit EvidenceLog(const std::string& path);

  class Record {
  public:
    Record(EvidenceLog& owner, const std::string& event);
    ~Record(); // commits the line to the owner
    Record(const Record&) = delete;
    Record& operator=(const Record&) = delete;

    Record& field(const std::string& key, const std::string& value);
    Record& field(const std::string& key, const char* value);
    Record& field(const std::string& key, int64_t value);
    Record& field(const std::string& key, uint64_t value);
    Record& field(const std::string& key, bool value);

  private:
    EvidenceLog& owner_;
    std::ostringstream body_;
    bool first_ = true;
  };

  Record event(const std::string& name);
  uint64_t sequence() const { return sequence_; }
  void flush();

private:
  friend class Record;
  void commit(const std::string& body);

  std::ofstream out_;
  std::mutex mutex_;
  uint64_t sequence_ = 0;
};

// Raw RGBA8 framebuffer capture kept as binary evidence (P6 PPM, alpha dropped).
bool write_ppm(const std::string& path, uint32_t width, uint32_t height,
               const std::vector<uint8_t>& rgba);

// Text-only pixel hash: cheap checksum used inside JSONL records.
std::string pixel_fingerprint(const std::vector<uint8_t>& rgba);

} // namespace vox
