#pragma once

#include "core/types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace clipfix {

Mesh loadObj(const std::filesystem::path& path);
void writeObj(
    const std::filesystem::path& path,
    const Mesh& mesh,
    const std::vector<std::string>& headerComments = {});

void recomputeVertexNormals(Mesh& mesh);
std::string positionsDigest(const std::vector<Vec3>& positions);

}  // namespace clipfix
