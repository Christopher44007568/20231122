#pragma once

#include "rtsync/json_util.hpp"
#include "rtsync/types.hpp"

#include <string>

namespace rtsync {

bool parse_trace(const Json& root, Trace* trace, std::string* error);
Json trace_summary_json(const Trace& trace);
std::uint64_t count_trace_records(const Trace& trace);

}  // namespace rtsync

