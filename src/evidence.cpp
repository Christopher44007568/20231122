#include "evidence.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace skr {

void writePpm(const std::string& path, const uint8_t* rgba,
              uint32_t width, uint32_t height) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write image: " + path);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
    for (size_t i = 0, n = static_cast<size_t>(width) * height; i < n; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    out.write(reinterpret_cast<const char*>(rgb.data()),
              static_cast<std::streamsize>(rgb.size()));
}

void writeDiffPpm(const std::string& path, const Snapshot& a,
                  const Snapshot& b) {
    const uint32_t width = a.width;
    const uint32_t height = a.height;
    const size_t pixels = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgb(pixels * 3 * 3);
    for (size_t i = 0; i < pixels; ++i) {
        for (int panel = 0; panel < 3; ++panel) {
            uint8_t r, g, bl;
            if (panel == 0) {
                r = a.pixels[i * 4 + 0];
                g = a.pixels[i * 4 + 1];
                bl = a.pixels[i * 4 + 2];
            } else if (panel == 1) {
                r = b.pixels[i * 4 + 0];
                g = b.pixels[i * 4 + 1];
                bl = b.pixels[i * 4 + 2];
            } else {
                const int dr = std::abs(int(a.pixels[i * 4 + 0]) - int(b.pixels[i * 4 + 0]));
                const int dg = std::abs(int(a.pixels[i * 4 + 1]) - int(b.pixels[i * 4 + 1]));
                const int db = std::abs(int(a.pixels[i * 4 + 2]) - int(b.pixels[i * 4 + 2]));
                r = static_cast<uint8_t>(std::min(255, dr * 8));
                g = static_cast<uint8_t>(std::min(255, dg * 8));
                bl = static_cast<uint8_t>(std::min(255, db * 8));
            }
            const size_t outIndex = (i + static_cast<size_t>(panel) * pixels) * 3;
            rgb[outIndex + 0] = r;
            rgb[outIndex + 1] = g;
            rgb[outIndex + 2] = bl;
        }
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write diff image: " + path);
    out << "P6\n" << (width * 3) << ' ' << height << "\n255\n";
    out.write(reinterpret_cast<const char*>(rgb.data()),
              static_cast<std::streamsize>(rgb.size()));
}

ImageDelta compareImages(const Snapshot& a, const Snapshot& b) {
    ImageDelta delta;
    if (a.width != b.width || a.height != b.height ||
        a.pixels.size() != b.pixels.size())
        throw std::runtime_error("snapshot dimensions do not match");
    delta.totalPixels = static_cast<uint64_t>(a.width) * a.height;
    uint64_t channelErrorSum = 0;
    const size_t channels = delta.totalPixels * 4;
    for (size_t i = 0; i < channels; ++i) {
        const int d = std::abs(int(a.pixels[i]) - int(b.pixels[i]));
        if (d != 0 && (i % 4) < 3) {
            if ((i % 4) == 0) ++delta.differingPixels;
        }
        channelErrorSum += d;
        delta.maxChannelError =
            std::max(delta.maxChannelError, static_cast<uint32_t>(d));
    }
    delta.meanAbsError =
        static_cast<double>(channelErrorSum) / static_cast<double>(channels);
    return delta;
}

namespace {

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out + "\"";
}

std::string transitionsJson(const std::vector<ResourceStateEvent>& events) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < events.size(); ++i) {
        const ResourceStateEvent& e = events[i];
        os << (i ? "," : "") << "{"
           << "\"resource\":" << quote(e.resource) << ","
           << "\"srcStageMask\":" << quote(e.stageMask) << ","
           << "\"dstAccessStage\":" << quote(e.accessMask) << ","
           << "\"oldLayout\":" << quote(e.oldLayout) << ","
           << "\"newLayout\":" << quote(e.newLayout) << "}";
    }
    return os.str() + "]";
}

} // namespace

std::string snapshotToJson(const Snapshot& s) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(9);
    os << "{"
       << "\"label\":" << quote(s.label) << ","
       << "\"formatRevision\":" << static_cast<uint32_t>(s.revision) << ","
       << "\"objectName\":" << quote(s.objectName) << ","
       << "\"device\":" << quote(s.deviceName) << ","
       << "\"frameId\":" << s.frameId << ","
       << "\"framebuffer\":{"
       << "\"name\":" << quote(s.framebuffer.name) << ","
       << "\"width\":" << s.framebuffer.width << ","
       << "\"height\":" << s.framebuffer.height << ","
       << "\"colorFormat\":\"VK_FORMAT_R8G8B8A8_UNORM\","
       << "\"depthFormat\":\"VK_FORMAT_D32_SFLOAT\","
       << "\"samples\":1,"
       << "\"finalColorLayout\":\"VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL\","
       << "\"transitions\":" << transitionsJson(s.framebuffer.transitions)
       << "},"
       << "\"drawCall\":{"
       << "\"index\":" << s.drawCall.index << ","
       << "\"vertexCount\":" << s.drawCall.vertexCount << ","
       << "\"instanceCount\":" << s.drawCall.instanceCount << ","
       << "\"firstVertex\":" << s.drawCall.firstVertex << ","
       << "\"firstInstance\":" << s.drawCall.firstInstance << ","
       << "\"bindPoint\":\"VK_PIPELINE_BIND_POINT_GRAPHICS\","
       << "\"pipeline\":" << quote(s.drawCall.pipelineName) << ","
       << "\"framebuffer\":" << quote(s.drawCall.framebufferName) << ","
       << "\"vertexBindings\":[";
    for (size_t i = 0; i < s.drawCall.vertexBindings.size(); ++i)
        os << (i ? "," : "") << quote(s.drawCall.vertexBindings[i]);
    os << "],"
       << "\"indexBuffer\":" << quote(s.drawCall.indexBuffer) << ","
       << "\"skinMatrixCount\":" << s.drawCall.skinMatrixCount
       << "},"
       << "\"resourceTransitions\":" << transitionsJson(s.allTransitions) << ","
       << "\"imagePath\":" << quote(s.imagePath)
       << "}";
    return os.str();
}

std::string correlationToJson(const Snapshot& legacy, const Snapshot& current,
                              const ImageDelta& delta,
                              const std::string& diffImagePath,
                              bool cpuGpuConsistent,
                              double cpuGpuMaxPositionError,
                              bool imageEquivalent,
                              const std::string& verdict,
                              const std::string& deltaAttribution) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(9);
    os << "{"
       << "\"legacySnapshot\":" << snapshotToJson(legacy) << ","
       << "\"currentSnapshot\":" << snapshotToJson(current) << ","
       << "\"correlation\":{"
       << "\"drawCallCountMatch\":true,"
       << "\"legacyVertexCount\":" << legacy.drawCall.vertexCount << ","
       << "\"currentVertexCount\":" << current.drawCall.vertexCount << ","
       << "\"pipelineMatch\":"
       << (legacy.drawCall.pipelineName == current.drawCall.pipelineName
               ? "true" : "false") << ","
       << "\"framebufferMatch\":{"
       << "\"width\":" << (legacy.framebuffer.width == current.framebuffer.width)
       << ",\"height\":"
       << (legacy.framebuffer.height == current.framebuffer.height)
       << ",\"colorFormat\":"
       << (legacy.framebuffer.colorFormat == current.framebuffer.colorFormat)
       << "},"
       << "\"resourceStateTransitionCount\":["
       << legacy.allTransitions.size() << "," << current.allTransitions.size()
       << "],"
       << "\"imageDelta\":{"
       << "\"differingPixels\":" << delta.differingPixels << ","
       << "\"totalPixels\":" << delta.totalPixels << ","
       << "\"meanAbsError\":" << delta.meanAbsError << ","
       << "\"maxChannelError\":" << delta.maxChannelError
       << "},"
       << "\"diffImagePath\":" << quote(diffImagePath) << ","
       << "\"cpuGpuSkinningConsistent\":"
       << (cpuGpuConsistent ? "true" : "false") << ","
       << "\"cpuGpuMaxPositionError\":" << cpuGpuMaxPositionError << ","
       << "\"imageEquivalentWithinTolerance\":"
       << (imageEquivalent ? "true" : "false") << ","
       << "\"verdict\":" << quote(verdict) << ","
       << "\"deltaAttribution\":" << quote(deltaAttribution)
       << "}}";
    return os.str();
}

} // namespace skr
