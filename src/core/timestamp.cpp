#include "core/timestamp.h"
#include "core/util.h"

namespace ssor {

TimestampPoint nowTimestamp() {
    return {utcNowMicros(), epochMillis(), monotonicNanos()};
}

}  // namespace ssor
