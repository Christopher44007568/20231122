#pragma once

#include "core/types.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace clipfix {

struct MaterialFile {
    std::string relativePath;
    std::string sha256;
    std::uint64_t bytes = 0;
};

struct RawMaterialBundle {
    std::filesystem::path root;
    RawScene scene;
    std::string manifestSha256;
    std::string aggregateSha256;
    std::vector<MaterialFile> files;
};

RawMaterialBundle loadRawMaterial(const std::filesystem::path& root);
bool verifyRawMaterial(
    const RawMaterialBundle& bundle,
    std::string* error = nullptr);
bool verifyRawMaterialDirectory(
    const std::filesystem::path& root,
    RawMaterialBundle* loaded = nullptr,
    std::string* error = nullptr);
void copyRawMaterial(
    const RawMaterialBundle& bundle,
    const std::filesystem::path& destination);

}  // namespace clipfix
