#pragma once

#include <cstdint>
#include <string>

namespace ssor {

struct TimestampPoint {
    std::string utc;
    std::int64_t epochMs = 0;
    std::int64_t monotonicNs = 0;
};

TimestampPoint nowTimestamp();

}  // namespace ssor
