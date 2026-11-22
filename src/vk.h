#pragma once
#include "image.h"
#include <cstdint>
#include <string>

typedef struct GLFWwindow GLFWwindow;

namespace tcpv {

class EventLog; // evidence.h

// One GPU texture resource tracked for evidence correlation.
struct GpuTexture {
  std::string resourceId; // e.g. "tex/gradient/BC1"
  uint64_t image = 0;     // VkImage
  uint64_t view = 0;      // VkImageView
  uint64_t memory = 0;    // VkDeviceMemory
};

struct RenderResult {
  bool ok = false;
  std::string error;
  Image captured; // framebuffer readback
  std::string framebufferId;
  std::string drawCallId;
};

class VkEngine {
public:
  struct Impl; // defined in vk.cpp

  // windowed: create a GLFW window (for interactive cancel); otherwise the
  // GLFW null platform is used and rendering stays fully offscreen.
  bool init(bool windowed, EventLog& log);
  void shutdown();
  bool ok() const { return device_ != nullptr; }
  const std::string& error() const { return error_; }
  std::string deviceName() const { return deviceName_; }

  // Upload RGBA pixels as a sampled texture. Layout transitions are logged
  // to the EventLog as resource-state evidence.
  GpuTexture uploadTexture(const std::string& resourceId, const Image& img);
  void destroyTexture(GpuTexture& tex);

  // Draw one fullscreen textured quad into a fresh offscreen framebuffer,
  // read it back, and return the captured pixels. Draw call, framebuffer
  // and resource-state events are correlated via drawCallId.
  RenderResult renderQuad(const GpuTexture& tex, uint32_t w, uint32_t h,
                          const std::string& drawCallId);

  // Pump GLFW events; returns true if the user requested cancel (ESC/close).
  bool cancelRequested();
  double timeSeconds() const;

private:
  Impl* impl_ = nullptr;
  void* device_ = nullptr; // opaque "is ready" flag
  std::string error_;
  std::string deviceName_;
  EventLog* log_ = nullptr;
  GLFWwindow* window_ = nullptr;
};

} // namespace tcpv
