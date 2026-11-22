#pragma once

#include <cstdint>

#include "common.hpp"

namespace nmts {

struct Epsilons {
  float orthogonal;
  float unit;
  float normal_z;
};

struct VerifyInput {
  InstanceRecord instance;
  std::uint8_t pixel[4];
  Epsilons eps;
};

// Pure, allocation-free check kernel. The Vulkan compute shader mirrors this
// function line for line so both backends produce bit-identical results.
ResultRecord verify_one(std::uint32_t instance_index,
                        const VerifyInput& in);

// Quantization applied to every emitted metric, on every backend, so that
// GPU/CPU and pause/resume outputs compare exactly.
float quantize_metric(float v);

}  // namespace nmts
