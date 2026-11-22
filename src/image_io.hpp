#pragma once

#include "compare.hpp"

#include <filesystem>
#include <string>

bool writePng(const std::filesystem::path& path, const FrameData& frame,
              std::string& error);

