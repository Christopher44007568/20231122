#pragma once

#include <memory>
#include <string>
#include <vector>

#include "model.h"
#include "util.h"

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual bool renderFrame(const RenderParams& params, Image& output, std::string& error) = 0;
    virtual Json diagnostics() const = 0;
    virtual std::string backendName() const = 0;
};

uint64_t estimateTransientBytes(const Config& config);
RenderPlan makeRenderPlan(const Config& config, uint64_t budgetBytes);
std::unique_ptr<Renderer> createRenderer(const Config& config,
                                         RenderPlan& plan,
                                         std::vector<ResourceEvent>& events);
std::unique_ptr<Renderer> createCpuRenderer();

