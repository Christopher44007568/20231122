#include "core/obj_io.h"

#include "core/sha256.h"
#include "core/time_util.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace clipfix {
namespace {

std::uint32_t parseObjIndex(const std::string& token, std::size_t vertexCount) {
    const auto slash = token.find('/');
    const std::string number = token.substr(0, slash);
    if (number.empty()) {
        throw std::runtime_error("empty OBJ index");
    }
    const long long raw = std::stoll(number);
    long long resolved = raw;
    if (raw < 0) {
        resolved = static_cast<long long>(vertexCount) + raw + 1;
    }
    if (resolved <= 0 || static_cast<std::size_t>(resolved) > vertexCount) {
        throw std::runtime_error("OBJ index out of range: " + token);
    }
    return static_cast<std::uint32_t>(resolved - 1);
}

}  // namespace

Mesh loadObj(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot read OBJ: " + path.string());
    }

    Mesh mesh;
    std::vector<std::array<std::uint32_t, 3>> faces;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream row(line);
        std::string tag;
        row >> tag;
        try {
            if (tag == "v") {
                Vec3 point;
                row >> point.x >> point.y >> point.z;
                if (!row) {
                    throw std::runtime_error("bad vertex");
                }
                mesh.positions.push_back(point);
            } else if (tag == "vn") {
                Vec3 normal;
                row >> normal.x >> normal.y >> normal.z;
                if (!row) {
                    throw std::runtime_error("bad normal");
                }
                mesh.normals.push_back(normal);
            } else if (tag == "f") {
                std::vector<std::uint32_t> polygon;
                std::string token;
                while (row >> token) {
                    polygon.push_back(parseObjIndex(token, mesh.positions.size()));
                }
                if (polygon.size() < 3) {
                    throw std::runtime_error("face has fewer than 3 vertices");
                }
                for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
                    faces.push_back({polygon[0], polygon[i], polygon[i + 1]});
                }
            }
        } catch (const std::exception& error) {
            throw std::runtime_error(
                path.string() + ":" + std::to_string(lineNumber) + ": " + error.what());
        }
    }

    mesh.indices.reserve(faces.size() * 3);
    for (const auto& face : faces) {
        mesh.indices.insert(mesh.indices.end(), face.begin(), face.end());
    }
    if (mesh.normals.size() != mesh.positions.size()) {
        recomputeVertexNormals(mesh);
    }
    if (!mesh.valid()) {
        throw std::runtime_error("OBJ has no renderable geometry: " + path.string());
    }
    return mesh;
}

void writeObj(
    const std::filesystem::path& path,
    const Mesh& mesh,
    const std::vector<std::string>& headerComments) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot write OBJ: " + path.string());
    }
    output << "# Curved Trim Plane - generated export\n";
    for (const auto& comment : headerComments) {
        output << "# " << comment << '\n';
    }
    output << std::fixed << std::setprecision(9);
    for (const auto& point : mesh.positions) {
        output << "v " << point.x << ' ' << point.y << ' ' << point.z << '\n';
    }

    std::vector<Vec3> normals = mesh.normals;
    if (normals.size() != mesh.positions.size()) {
        Mesh copy = mesh;
        recomputeVertexNormals(copy);
        normals = std::move(copy.normals);
    }
    for (const auto& normal : normals) {
        output << "vn " << normal.x << ' ' << normal.y << ' ' << normal.z << '\n';
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const auto a = mesh.indices[i] + 1;
        const auto b = mesh.indices[i + 1] + 1;
        const auto c = mesh.indices[i + 2] + 1;
        output << "f " << a << "//" << a << ' ' << b << "//" << b << ' '
               << c << "//" << c << '\n';
    }
    if (!output) {
        throw std::runtime_error("failed while writing OBJ: " + path.string());
    }
}

void recomputeVertexNormals(Mesh& mesh) {
    mesh.normals.assign(mesh.positions.size(), Vec3{});
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const auto ia = mesh.indices[i];
        const auto ib = mesh.indices[i + 1];
        const auto ic = mesh.indices[i + 2];
        const Vec3 edgeA = mesh.positions[ib] - mesh.positions[ia];
        const Vec3 edgeB = mesh.positions[ic] - mesh.positions[ia];
        const Vec3 faceNormal = edgeA.cross(edgeB);
        mesh.normals[ia] += faceNormal;
        mesh.normals[ib] += faceNormal;
        mesh.normals[ic] += faceNormal;
    }
    for (auto& normal : mesh.normals) {
        normal = normal.normalized();
    }
}

std::string positionsDigest(const std::vector<Vec3>& positions) {
    Sha256 hash;
    for (const auto& point : positions) {
        hash.update(formatFixed(point.x, 9));
        hash.update(",");
        hash.update(formatFixed(point.y, 9));
        hash.update(",");
        hash.update(formatFixed(point.z, 9));
        hash.update(";");
    }
    return toHex(hash.finish());
}

}  // namespace clipfix
