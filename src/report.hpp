#pragma once

#include "json.hpp"

#include <string>

namespace kfc {

std::string makeReviewMarkdown(const Json& record);

}  // namespace kfc
