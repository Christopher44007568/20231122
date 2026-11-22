#pragma once

#include "rtsync/json_util.hpp"

#include <string>

namespace rtsync {

struct RenderProbe {
  bool glfw_initialized = false;
  bool vulkan_initialized = false;
  std::string device_name;
  std::string api_version;
  std::uint32_t width = 320;
  std::uint32_t height = 200;
  std::string golden_sha256;
  std::string observed_sha256;
  std::string command_buffer_id = "vk-cmd-demo";
  std::string fence_id = "vk-fence-demo";
  bool fence_signaled = false;
  bool fence_waited_after_submit = false;
};

bool render_demo_frames(const std::string& output_dir, RenderProbe* probe,
                        std::string* error);

}  // namespace rtsync

