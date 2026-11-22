#pragma once

#include "common.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace offscreen {

struct Event {
    std::uint32_t id = 0;
    std::string op;
    std::string arg0;
    std::string arg1;
    std::string raw;
    std::size_t line = 0;
};

struct Record {
    std::filesystem::path path;
    std::size_t byteCount = 0;
    std::size_t completePrefixBytes = 0;
    std::string sha256;
    std::vector<Event> events;
    bool truncatedTail = false;
    std::string incompleteTail;
};

Record loadRecord(const std::filesystem::path& path);
std::string serializeEvent(const Event& event);
std::string serializeRecord(const std::vector<Event>& events);
void writeRecord(const std::filesystem::path& path, const std::vector<Event>& events);

// Produces complete event lines through keepThrough, then an intentionally
// unterminated prefix of the following event (or a cut inside an event line).
std::string makeTruncatedRecord(const std::vector<Event>& events,
                                std::size_t keepThrough,
                                std::size_t partialBytes);

}  // namespace offscreen
