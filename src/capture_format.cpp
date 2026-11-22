#include "capture_format.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace ptdc {

namespace {

template <typename T>
void writePod(std::ostream& os, const T& v) {
  os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
T readPod(std::istream& is, const char* what) {
  T v{};
  if (!is.read(reinterpret_cast<char*>(&v), sizeof(T)))
    throw std::runtime_error(std::string("capture truncated while reading ") + what);
  return v;
}

void writeBytes(std::ostream& os, const void* data, size_t size) {
  os.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}

std::vector<uint8_t> readBytes(std::istream& is, size_t size, const char* what) {
  std::vector<uint8_t> out(size);
  if (size && !is.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size)))
    throw std::runtime_error(std::string("capture truncated while reading ") + what);
  return out;
}

void writeString(std::ostream& os, const std::string& s) {
  writePod(os, static_cast<uint32_t>(s.size()));
  writeBytes(os, s.data(), s.size());
}

std::string readString(std::istream& is, const char* what) {
  uint32_t n = readPod<uint32_t>(is, what);
  if (n > (1u << 20)) throw std::runtime_error(std::string("unreasonable string length in ") + what);
  auto bytes = readBytes(is, n, what);
  return std::string(bytes.begin(), bytes.end());
}

}  // namespace

uint64_t fnv1a64(const void* data, size_t size) {
  const auto* p = static_cast<const uint8_t*>(data);
  uint64_t h = 14695981039346656037ull;
  for (size_t i = 0; i < size; ++i) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

const char* topologyName(uint32_t t) {
  switch (t) {
    case 0: return "POINT_LIST";
    case 1: return "LINE_LIST";
    case 2: return "LINE_STRIP";
    case 3: return "TRIANGLE_LIST";
    case 4: return "TRIANGLE_STRIP";
    case 5: return "TRIANGLE_FAN";
    case 6: return "LINE_LIST_WITH_ADJACENCY";
    case 7: return "LINE_STRIP_WITH_ADJACENCY";
    case 8: return "TRIANGLE_LIST_WITH_ADJACENCY";
    case 9: return "TRIANGLE_STRIP_WITH_ADJACENCY";
    case 10: return "PATCH_LIST";
    default: return "UNKNOWN";
  }
}

const char* resourceStateName(ResourceState s) {
  switch (s) {
    case ResourceState::Undefined: return "UNDEFINED";
    case ResourceState::TransferDst: return "TRANSFER_DST";
    case ResourceState::VertexBuffer: return "VERTEX_BUFFER";
    case ResourceState::ColorAttachment: return "COLOR_ATTACHMENT";
    case ResourceState::ShaderRead: return "SHADER_READ";
  }
  return "?";
}

void saveCapture(const Capture& cap, const std::string& path) {
  std::ofstream os(path, std::ios::binary | std::ios::trunc);
  if (!os) throw std::runtime_error("cannot open capture for writing: " + path);

  writePod(os, kMagic);
  writePod(os, cap.version);
  writePod(os, cap.width);
  writePod(os, cap.height);

  if (cap.version == kVersionCurrent) {
    writePod(os, static_cast<uint32_t>(cap.extensions.size()));
    for (const auto& e : cap.extensions) {
      writeString(os, e.name);
      writePod(os, e.specVersion);
      writePod(os, static_cast<uint8_t>(e.required ? 1 : 0));
    }
    writePod(os, static_cast<uint32_t>(cap.resources.size()));
    for (const auto& r : cap.resources) {
      writePod(os, r.id);
      writePod(os, static_cast<uint32_t>(r.type));
      writePod(os, r.size);
      writeString(os, r.debugName);
      writePod(os, static_cast<uint32_t>(r.timeline.size()));
      for (const auto& t : r.timeline) {
        writePod(os, t.drawIndex);
        writePod(os, static_cast<uint32_t>(t.state));
      }
    }
  }

  writePod(os, static_cast<uint32_t>(cap.draws.size()));
  for (const auto& d : cap.draws) {
    writePod(os, d.topology);
    writePod(os, d.firstVertex);
    writePod(os, d.vertexCount);
    if (cap.version == kVersionCurrent) {
      writePod(os, static_cast<uint32_t>(d.resourceRefs.size()));
      for (uint32_t id : d.resourceRefs) writePod(os, id);
    }
  }

  writePod(os, static_cast<uint32_t>(cap.vertices.size()));
  if (!cap.vertices.empty())
    writeBytes(os, cap.vertices.data(), cap.vertices.size() * sizeof(Vertex));

  if (cap.version == kVersionCurrent) {
    writePod(os, static_cast<uint64_t>(cap.referenceImage.size()));
    if (!cap.referenceImage.empty()) writeBytes(os, cap.referenceImage.data(), cap.referenceImage.size());
  }
  writePod(os, cap.referenceHash);
  if (!os) throw std::runtime_error("failed while writing capture: " + path);
}

Capture loadCapture(const std::string& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) throw std::runtime_error("cannot open capture: " + path);

  Capture cap;
  cap.sourcePath = path;
  if (readPod<uint32_t>(is, "magic") != kMagic)
    throw std::runtime_error("not a PTDC capture (bad magic): " + path);
  cap.version = readPod<uint32_t>(is, "version");
  if (cap.version != kVersionLegacy && cap.version != kVersionCurrent)
    throw std::runtime_error("unsupported PTDC version " + std::to_string(cap.version) +
                             " (this build understands v1 and v2)");
  cap.width = readPod<uint32_t>(is, "width");
  cap.height = readPod<uint32_t>(is, "height");

  if (cap.version == kVersionCurrent) {
    uint32_t nExt = readPod<uint32_t>(is, "extension count");
    for (uint32_t i = 0; i < nExt; ++i) {
      ExtensionRecord e;
      e.name = readString(is, "extension name");
      e.specVersion = readPod<uint32_t>(is, "extension spec version");
      e.required = readPod<uint8_t>(is, "extension flags") != 0;
      cap.extensions.push_back(std::move(e));
    }
    uint32_t nRes = readPod<uint32_t>(is, "resource count");
    for (uint32_t i = 0; i < nRes; ++i) {
      ResourceRecord r;
      r.id = readPod<uint32_t>(is, "resource id");
      r.type = static_cast<ResourceType>(readPod<uint32_t>(is, "resource type"));
      r.size = readPod<uint64_t>(is, "resource size");
      r.debugName = readString(is, "resource name");
      uint32_t nT = readPod<uint32_t>(is, "resource timeline count");
      for (uint32_t j = 0; j < nT; ++j) {
        ResourceStateEntry t;
        t.drawIndex = readPod<uint32_t>(is, "timeline draw index");
        t.state = static_cast<ResourceState>(readPod<uint32_t>(is, "timeline state"));
        r.timeline.push_back(t);
      }
      cap.resources.push_back(std::move(r));
    }
  }

  uint32_t nDraws = readPod<uint32_t>(is, "draw count");
  for (uint32_t i = 0; i < nDraws; ++i) {
    DrawCall d;
    d.topology = readPod<uint32_t>(is, "draw topology");
    d.firstVertex = readPod<uint32_t>(is, "draw first vertex");
    d.vertexCount = readPod<uint32_t>(is, "draw vertex count");
    if (cap.version == kVersionCurrent) {
      uint32_t nRef = readPod<uint32_t>(is, "draw resource refs");
      for (uint32_t j = 0; j < nRef; ++j) d.resourceRefs.push_back(readPod<uint32_t>(is, "resource ref"));
    }
    cap.draws.push_back(std::move(d));
  }

  uint32_t nVerts = readPod<uint32_t>(is, "vertex count");
  cap.vertices.resize(nVerts);
  if (nVerts) {
    auto bytes = readBytes(is, static_cast<size_t>(nVerts) * sizeof(Vertex), "vertex blob");
    std::memcpy(cap.vertices.data(), bytes.data(), bytes.size());
  }

  if (cap.version == kVersionCurrent) {
    uint64_t imgSize = readPod<uint64_t>(is, "reference image size");
    cap.referenceImage = readBytes(is, static_cast<size_t>(imgSize), "reference image");
  }
  cap.referenceHash = readPod<uint64_t>(is, "reference hash");
  return cap;
}

}  // namespace ptdc
