#include "evidence.h"

#include <chrono>
#include <iomanip>

namespace vox {

std::string json_escape(const std::string& raw) {
  std::string out;
  out.reserve(raw.size() + 2);
  for (char c : raw) {
    unsigned char uc = static_cast<unsigned char>(c);
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\b': out += "\\b"; break;
    case '\f': out += "\\f"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (uc < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", uc);
        out += buf;
      } else {
        out += c;
      }
    }
  }
  return out;
}

static uint64_t now_millis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}

EvidenceLog::EvidenceLog(const std::string& path) : out_(path) {
  if (!out_) {
    throw std::runtime_error("cannot open evidence log: " + path);
  }
}

EvidenceLog::Record EvidenceLog::event(const std::string& name) {
  return Record(*this, name);
}

void EvidenceLog::commit(const std::string& body) {
  std::lock_guard<std::mutex> lock(mutex_);
  ++sequence_;
  out_ << "{\"seq\":" << sequence_ << ",\"t_ms\":" << now_millis() << body
       << "}\n";
}

void EvidenceLog::flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  out_.flush();
}

EvidenceLog::Record::Record(EvidenceLog& owner, const std::string& event)
    : owner_(owner) {
  body_ << ",\"event\":\"" << json_escape(event) << "\"";
}

EvidenceLog::Record::~Record() { owner_.commit(body_.str()); }

EvidenceLog::Record& EvidenceLog::Record::field(const std::string& key,
                                                const std::string& value) {
  body_ << ",\"" << json_escape(key) << "\":\"" << json_escape(value) << "\"";
  return *this;
}

EvidenceLog::Record& EvidenceLog::Record::field(const std::string& key,
                                                const char* value) {
  return field(key, std::string(value));
}

EvidenceLog::Record& EvidenceLog::Record::field(const std::string& key,
                                                int64_t value) {
  body_ << ",\"" << json_escape(key) << "\":" << value;
  return *this;
}

EvidenceLog::Record& EvidenceLog::Record::field(const std::string& key,
                                                uint64_t value) {
  body_ << ",\"" << json_escape(key) << "\":" << value;
  return *this;
}

EvidenceLog::Record& EvidenceLog::Record::field(const std::string& key,
                                                bool value) {
  body_ << ",\"" << json_escape(key) << "\":" << (value ? "true" : "false");
  return *this;
}

bool write_ppm(const std::string& path, uint32_t width, uint32_t height,
               const std::vector<uint8_t>& rgba) {
  std::ofstream f(path, std::ios::binary);
  if (!f || rgba.size() < size_t(width) * height * 4) return false;
  f << "P6\n" << width << " " << height << "\n255\n";
  std::vector<uint8_t> rgb(size_t(width) * height * 3);
  for (size_t i = 0, p = 0; i < rgba.size(); i += 4, p += 3) {
    rgb[p] = rgba[i];
    rgb[p + 1] = rgba[i + 1];
    rgb[p + 2] = rgba[i + 2];
  }
  f.write(reinterpret_cast<const char*>(rgb.data()),
          static_cast<std::streamsize>(rgb.size()));
  return static_cast<bool>(f);
}

std::string pixel_fingerprint(const std::vector<uint8_t>& rgba) {
  uint64_t h1 = 1469598103934665603ull;
  uint64_t h2 = 1469598103934665603ull;
  for (size_t i = 0; i < rgba.size(); ++i) {
    h1 ^= rgba[i];
    h1 *= 1099511628211ull;
    if (i % 2 == 0) {
      h2 ^= rgba[i];
      h2 *= 1099511628211ull;
    }
  }
  char buf[33];
  std::snprintf(buf, sizeof(buf), "%016lx%016lx",
                static_cast<unsigned long>(h1),
                static_cast<unsigned long>(h2));
  return buf;
}

} // namespace vox
