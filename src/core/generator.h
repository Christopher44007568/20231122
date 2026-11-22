#pragma once

#include "core/material.h"

#include <string>

namespace ssor {

RawMaterial makeCanonicalMaterial();
RawMaterial makeEmptyBoundaryMaterial();
bool writeCanonicalMaterials(const std::string& rawDir);

}  // namespace ssor
