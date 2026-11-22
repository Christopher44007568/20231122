#pragma once

#include <map>
#include <set>
#include <string>

#include <picojson.h>

#include "model.hpp"

namespace bbox {

struct RenderOptions {
  int frames = 2;
  bool visible = false;
  std::string screenshot_path;
};

struct RenderResult {
  bool attempted = false;
  bool ok = false;
  bool skipped = false;
  std::string backend = "Vulkan 1.0 / GLFW";
  std::string adapter;
  std::string reason;
  std::string screenshot_sha256;
  int width = 0;
  int height = 0;
};

RenderResult renderModels(const std::map<std::string, Model>& models,
                          const std::set<std::string>& exception_objects,
                          const RenderOptions& options);

picojson::value renderResultJson(const RenderResult& result);

}  // namespace bbox
