#pragma once

#include <string>

#include "camtrak/types.hpp"

namespace camtrak {

// Sorts samples by device timestamp and validates structural invariants.
// Throws std::runtime_error with a descriptive message on bad input.
void sortAndValidate(Trajectory& trajectory);

// Finds the bracketing samples around reference time `t` on the given
// trajectory. Returns false when `t` is outside the sampled range.
bool bracket(const std::vector<Sample>& samples, double t,
             const Sample*& before, const Sample*& after, double& u);

InterpMethod parseMethod(const std::string& name);
const char* methodName(InterpMethod method);

}  // namespace camtrak
