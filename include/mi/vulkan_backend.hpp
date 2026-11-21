#pragma once

#include <memory>

#include "mi/backend.hpp"

namespace mi {

std::unique_ptr<GpuBackend> create_vulkan_backend(bool visible);

}  // namespace mi
