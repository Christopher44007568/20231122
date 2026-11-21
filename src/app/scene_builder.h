#pragma once

#include "app/app_state.h"

#include <cstdint>
#include <vector>

namespace clipfix {

struct RenderVertex {
    float position[3];
    float normal[3];
    float color[3];
};

struct RenderData {
    std::vector<RenderVertex> triangleVertices;
    std::vector<std::uint32_t> triangleIndices;
    std::vector<RenderVertex> lineVertices;
    std::vector<std::uint32_t> lineIndices;
    int selectedObject = 0;
};

RenderData buildRenderData(const AppState& app);

}  // namespace clipfix
