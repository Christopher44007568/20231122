#pragma once

#include "types.hpp"

#include <string>

namespace gcdump {

enum class ReportFormat { Markdown, PlainText };

std::string renderReport(const Report& report, ReportFormat format);

// One-line machine-readable summary used by the GUI's header strip.
std::string renderSummaryLine(const Report& report);

} // namespace gcdump
