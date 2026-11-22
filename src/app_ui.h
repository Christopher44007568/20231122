#pragma once

#include "hdr_provenance.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hdrprov {

enum class UiAction { None, Revalidate, Quit };

struct UiContext {
    BatchResult batch;
    std::vector<std::uint64_t> previews;
    std::filesystem::path root;
    int selected = 0;
    int selectedStep = -1;
    bool rawMode = false;
    std::string notice;
    double noticeUntil = 0.0;
};

UiAction drawUi(UiContext& context, double nowSeconds);

}  // namespace hdrprov
