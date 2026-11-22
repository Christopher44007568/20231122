#pragma once
#include "image.h"
#include <cstdint>
#include <string>
#include <vector>

namespace tcpv {

enum class CodecId { BC1, BC3, BC7 };

struct CodecInfo {
  CodecId id;
  const char* name;       // "BC1" / "BC3" / "BC7"
  const char* generation; // "legacy" (old format) or "new"
  uint32_t blockBytes;    // 8 or 16 bytes per 4x4 block
};

std::vector<CodecInfo> allCodecs();
const CodecInfo& codecInfo(CodecId id);
CodecId codecFromName(const std::string& name, bool& ok);

// Encode an RGBA image into 4x4 compressed blocks (row-major blocks).
std::vector<uint8_t> encodeBlocks(CodecId id, const Image& img);
// Decode blocks back to RGBA for upload/preview.
Image decodeBlocks(CodecId id, const uint8_t* data, uint32_t w, uint32_t h);

} // namespace tcpv
