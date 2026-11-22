#include "rtsync/clock.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rtsync {
namespace {

std::int64_t round_clamped(long double value) {
  if (!std::isfinite(static_cast<double>(value))) return 0;
  const long double low = static_cast<long double>(
      std::numeric_limits<std::int64_t>::min());
  const long double high = static_cast<long double>(
      std::numeric_limits<std::int64_t>::max());
  if (value <= low) return std::numeric_limits<std::int64_t>::min();
  if (value >= high) return std::numeric_limits<std::int64_t>::max();
  return static_cast<std::int64_t>(std::llround(value));
}

}  // namespace

ClockModel fit_clock(const Device& device) {
  ClockModel model;
  model.device_id = device.id;
  model.calibration_points = device.calibration.size();
  const long double nominal_slope =
      device.clock_frequency_hz == 0
          ? 0.0L
          : 1000000000.0L / static_cast<long double>(device.clock_frequency_hz);
  if (!(nominal_slope > 0.0L)) {
    model.error = "clock_frequency_hz must be positive";
    return model;
  }

  if (device.calibration.empty()) {
    model.method = "nominal_rate_only";
    model.slope_ns_per_tick = nominal_slope;
    model.intercept_ns = 0.0L;
    model.error = "no calibration points; absolute device-host relation unknown";
    return model;
  }

  if (device.calibration.size() == 1) {
    const ClockPoint& point = device.calibration.front();
    model.method = "single_point_nominal_rate";
    model.slope_ns_per_tick = nominal_slope;
    model.intercept_ns =
        static_cast<long double>(point.host_ns) -
        nominal_slope * static_cast<long double>(point.device_ticks);
    model.calibrated = true;
    model.valid = true;
    return model;
  }

  std::vector<ClockPoint> points = device.calibration;
  std::sort(points.begin(), points.end(), [](const ClockPoint& a,
                                             const ClockPoint& b) {
    if (a.host_ns != b.host_ns) return a.host_ns < b.host_ns;
    return a.device_ticks < b.device_ticks;
  });
  long double mean_x = 0.0L;
  long double mean_y = 0.0L;
  for (const ClockPoint& point : points) {
    mean_x += static_cast<long double>(point.device_ticks);
    mean_y += static_cast<long double>(point.host_ns);
  }
  mean_x /= static_cast<long double>(points.size());
  mean_y /= static_cast<long double>(points.size());
  long double numerator = 0.0L;
  long double denominator = 0.0L;
  for (const ClockPoint& point : points) {
    const long double dx =
        static_cast<long double>(point.device_ticks) - mean_x;
    const long double dy = static_cast<long double>(point.host_ns) - mean_y;
    numerator += dx * dy;
    denominator += dx * dx;
  }
  if (denominator <= 0.0L) {
    model.method = "least_squares";
    model.error = "calibration timestamps do not span distinct device ticks";
    return model;
  }
  model.method = "least_squares";
  model.slope_ns_per_tick = numerator / denominator;
  model.intercept_ns = mean_y - model.slope_ns_per_tick * mean_x;
  if (!(model.slope_ns_per_tick > 0.0L)) {
    model.error = "calibration produced a non-positive slope";
    return model;
  }
  long double residual = 0.0L;
  for (const ClockPoint& point : points) {
    const long double predicted =
        model.slope_ns_per_tick * static_cast<long double>(point.device_ticks) +
        model.intercept_ns;
    residual = std::max(
        residual, std::fabs(predicted - static_cast<long double>(point.host_ns)));
  }
  model.max_residual_ns = residual;
  model.calibrated = true;
  model.valid = true;
  return model;
}

std::int64_t map_device_ticks(const ClockModel& model, std::int64_t ticks) {
  if (!model.valid) return ticks;
  return round_clamped(model.slope_ns_per_tick *
                           static_cast<long double>(ticks) +
                       model.intercept_ns);
}

TimeSpan map_span(const ClockModel& model, std::int64_t start_ticks,
                  std::int64_t end_ticks) {
  TimeSpan span;
  span.raw_start = start_ticks;
  span.raw_end = end_ticks;
  if (end_ticks < start_ticks) std::swap(span.raw_start, span.raw_end);
  span.start_ns = map_device_ticks(model, span.raw_start);
  span.end_ns = map_device_ticks(model, span.raw_end);
  if (span.end_ns < span.start_ns) std::swap(span.start_ns, span.end_ns);
  return span;
}

bool spans_overlap(const TimeSpan& left, const TimeSpan& right) {
  return left.start_ns <= right.end_ns && right.start_ns <= left.end_ns;
}

}  // namespace rtsync

