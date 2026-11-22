#pragma once

#include <string>

namespace ssor {

struct AcceptOptions {
    std::string outDir = "artifacts/acceptance";
    std::string materialPath = "materials/raw/outline_repeat.ssoraw";
};

int runAcceptance(const AcceptOptions& options);

}  // namespace ssor
