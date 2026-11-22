#pragma once
#include "cloud.h"
#include "vk.h"
#include <string>

namespace pcd {

// Offscreen point-cloud snapshot (no swapchain; works headless).
// Writes a binary PPM image. Clipped points are tinted red.
void render_snapshot(VkContext& ctx, const Cloud& cloud,
                     const std::string& ppm_path, uint32_t width = 1024,
                     uint32_t height = 768);

} // namespace pcd
