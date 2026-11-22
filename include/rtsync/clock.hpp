#pragma once

#include "rtsync/types.hpp"

#include <cstdint>
#include <string>

namespace rtsync {

ClockModel fit_clock(const Device& device);
std::int64_t map_device_ticks(const ClockModel& model, std::int64_t ticks);
TimeSpan map_span(const ClockModel& model, std::int64_t start_ticks,
                  std::int64_t end_ticks);
bool spans_overlap(const TimeSpan& left, const TimeSpan& right);

}  // namespace rtsync

