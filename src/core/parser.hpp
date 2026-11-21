#pragma once

#include "types.hpp"

#include <iosfwd>
#include <string>

namespace gcdump {

struct ParseResult {
    std::vector<SourceLine> lines;
    std::vector<FieldRecord> fields;
    std::vector<ParseIssue> issues;
};

// Line-oriented dump grammar:
//   # comment                       -> retained as SourceLine, no field
//   key: value                      -> FieldRecord (first colon wins)
//   extension: <name> status=<...>  -> one FieldRecord per extension line
//   blank line                      -> retained as SourceLine only
// Unknown keys are preserved rather than rejected so an unknown extension
// stays auditable end to end.
ParseResult parseDump(std::istream& in);

std::string trim(std::string_view text);
std::string lower(std::string_view text);

} // namespace gcdump
