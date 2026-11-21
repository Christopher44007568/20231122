#include "core/time_util.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace clipfix {
namespace {

int millisecondsSinceEpoch(const std::chrono::system_clock::time_point& point) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        point.time_since_epoch());
    return static_cast<int>(ms.count() % 1000);
}

std::string utcString(std::time_t seconds, int millis) {
    std::tm utc{};
    gmtime_r(&seconds, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
        << std::setfill('0') << millis << 'Z';
    return out.str();
}

std::string localString(std::time_t seconds, int millis) {
    std::tm local{};
    localtime_r(&seconds, &local);
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
        << std::setfill('0') << millis << std::put_time(&local, "%z");
    return out.str();
}

}  // namespace

Timestamp nowTimestamp() {
    const auto point = std::chrono::system_clock::now();
    const auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        point.time_since_epoch());
    const std::time_t seconds = std::chrono::system_clock::to_time_t(point);
    const int millis = millisecondsSinceEpoch(point);

    Timestamp stamp;
    stamp.utc = utcString(seconds, millis);
    stamp.local = localString(seconds, millis);
    stamp.epochMs = static_cast<long long>(epochMs.count());
    return stamp;
}

std::string filenameStamp(const Timestamp& stamp) {
    std::string value = stamp.utc;
    for (char& ch : value) {
        if (ch == ':' || ch == '-' || ch == '.') {
            ch = '_';
        }
    }
    return value;
}

std::string formatFixed(double value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string formatMm(double value) {
    return formatFixed(value, 6) + " mm";
}

}  // namespace clipfix
