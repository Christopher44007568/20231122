#pragma once

#include "vegetation.hpp"

#include <string>
#include <vector>

namespace vegd {

class TileProcessor {
public:
    TileProcessor(Params params, ResourcePlan plan, bool forceCpu);
    ~TileProcessor();
    TileProcessor(const TileProcessor&) = delete;
    TileProcessor& operator=(const TileProcessor&) = delete;
    bool initialize(std::string& error);
    bool generate(uint32_t tile, TileData& result, std::string& error);

    const std::string& backendName() const { return backendName_; }
    const std::string& deviceName() const { return deviceName_; }
    const std::vector<std::string>& degradations() const { return degradations_; }
    uint32_t capacity() const { return plan_.capacity; }
    bool usingCpu() const { return backendName_ == "cpu"; }

private:
    bool fallbackToCpu(const std::string& reason);

    Params params_;
    ResourcePlan plan_;
    bool forceCpu_ = false;
    bool initialized_ = false;
    bool usingVulkan_ = false;
    std::string backendName_ = "cpu";
    std::string deviceName_ = "deterministic-cpu";
    std::vector<std::string> degradations_;
    void* vulkan_ = nullptr;
};

}  // namespace vegd
