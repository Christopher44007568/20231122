#pragma once

#include <string>

namespace clipfix {

struct Timestamp {
    std::string utc;
    std::string local;
    long long epochMs = 0;
};

Timestamp nowTimestamp();
std::string filenameStamp(const Timestamp& stamp);
std::string formatFixed(double value, int precision = 6);
std::string formatMm(double value);

}  // namespace clipfix
