#include "core/raw_material.h"

#include "core/obj_io.h"
#include "core/sha256.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace clipfix {
namespace {

using Json = nlohmann::json;

Json readJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot read JSON: " + path.string());
    }
    Json value;
    input >> value;
    return value;
}

Vec3 readVec3(const Json& value, const std::string& context) {
    if (!value.is_array() || value.size() != 3) {
        throw std::runtime_error(context + " must be a three-element array");
    }
    return {value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
}

std::string aggregateHash(const std::vector<MaterialFile>& files) {
    Sha256 hash;
    std::vector<MaterialFile> sorted = files;
    std::sort(
        sorted.begin(),
        sorted.end(),
        [](const MaterialFile& lhs, const MaterialFile& rhs) {
            return lhs.relativePath < rhs.relativePath;
        });
    for (const auto& file : sorted) {
        hash.update(file.relativePath);
        hash.update("\n");
        hash.update(file.sha256);
        hash.update("\n");
        hash.update(std::to_string(file.bytes));
        hash.update("\n");
    }
    return toHex(hash.finish());
}

RawScene parseScene(const Json& value) {
    RawScene scene;
    scene.format = value.value("format", "");
    scene.name = value.value("name", "");
    scene.description = value.value("description", "");
    scene.createdAt = value.value("created_at", "");
    if (scene.format != "curved-trim-raw-v1") {
        throw std::runtime_error("unsupported raw scene format: " + scene.format);
    }

    for (const auto& anchorJson : value.at("anchors")) {
        RawAnchor anchor;
        anchor.id = anchorJson.at("id").get<int>();
        anchor.name = anchorJson.at("name").get<std::string>();
        anchor.position = readVec3(anchorJson.at("position_mm"), "anchor position");
        anchor.normal = readVec3(anchorJson.at("normal"), "anchor normal").normalized();
        if (anchorJson.contains("raw_offset_text")) {
            anchor.rawOffsetText = anchorJson.at("raw_offset_text").get<std::string>();
            anchor.rawOffsetMm = std::stod(anchor.rawOffsetText);
            const double numericOffset = anchorJson.at("raw_offset_mm").get<double>();
            if (std::abs(numericOffset - anchor.rawOffsetMm) > 1.0e-9) {
                throw std::runtime_error(
                    "anchor raw_offset_text disagrees with raw_offset_mm");
            }
        } else {
            anchor.rawOffsetMm = anchorJson.at("raw_offset_mm").get<double>();
            anchor.rawOffsetText = std::to_string(anchor.rawOffsetMm);
        }
        scene.anchors.push_back(anchor);
    }
    if (scene.anchors.size() != 3) {
        throw std::runtime_error("raw scene must contain exactly three anchors");
    }

    for (const auto& objectJson : value.at("objects")) {
        RawObject object;
        object.id = objectJson.at("id").get<std::string>();
        object.name = objectJson.at("name").get<std::string>();
        object.relativeObjPath = objectJson.at("obj").get<std::string>();
        object.material = objectJson.value("material", "");
        scene.objects.push_back(object);
    }
    if (scene.objects.empty()) {
        throw std::runtime_error("raw scene must contain at least one render object");
    }
    return scene;
}

}  // namespace

RawMaterialBundle loadRawMaterial(const std::filesystem::path& root) {
    RawMaterialBundle bundle;
    bundle.root = root;

    const auto manifestPath = root / "MANIFEST.json";
    const Json manifest = readJson(manifestPath);
    if (manifest.value("format", "") != "clipfix-raw-material-v1") {
        throw std::runtime_error("unsupported raw material manifest format");
    }
    bundle.manifestSha256 = Sha256::fileHex(manifestPath);
    for (const auto& fileJson : manifest.at("files")) {
        MaterialFile file;
        file.relativePath = fileJson.at("path").get<std::string>();
        file.sha256 = fileJson.at("sha256").get<std::string>();
        file.bytes = fileJson.at("bytes").get<std::uint64_t>();
        bundle.files.push_back(file);
    }
    bundle.aggregateSha256 = aggregateHash(bundle.files);
    const std::string recordedAggregate = manifest.at("aggregate_sha256").get<std::string>();
    if (bundle.aggregateSha256 != recordedAggregate) {
        throw std::runtime_error("raw material manifest aggregate hash is inconsistent");
    }

    bundle.scene = parseScene(readJson(root / "scene.raw.json"));
    for (auto& object : bundle.scene.objects) {
        const auto fileIt = std::find_if(
            bundle.files.begin(),
            bundle.files.end(),
            [&object](const MaterialFile& file) {
                return file.relativePath == object.relativeObjPath;
            });
        if (fileIt == bundle.files.end()) {
            throw std::runtime_error("object OBJ is not listed in manifest: " + object.relativeObjPath);
        }
        object.rawFileSha256 = fileIt->sha256;
        object.mesh = loadObj(root / object.relativeObjPath);
    }
    return bundle;
}

bool verifyRawMaterial(const RawMaterialBundle& bundle, std::string* error) {
    try {
        for (const auto& file : bundle.files) {
            const auto fullPath = bundle.root / file.relativePath;
            if (!std::filesystem::is_regular_file(fullPath)) {
                throw std::runtime_error("missing raw material file: " + file.relativePath);
            }
            const auto size = std::filesystem::file_size(fullPath);
            if (size != file.bytes) {
                throw std::runtime_error("raw material size changed: " + file.relativePath);
            }
            if (Sha256::fileHex(fullPath) != file.sha256) {
                throw std::runtime_error("raw material checksum changed: " + file.relativePath);
            }
        }
        if (aggregateHash(bundle.files) != bundle.aggregateSha256) {
            throw std::runtime_error("raw material aggregate checksum changed");
        }
        return true;
    } catch (const std::exception& ex) {
        if (error != nullptr) {
            *error = ex.what();
        }
        return false;
    }
}

bool verifyRawMaterialDirectory(
    const std::filesystem::path& root,
    RawMaterialBundle* loaded,
    std::string* error) {
    try {
        RawMaterialBundle bundle = loadRawMaterial(root);
        if (!verifyRawMaterial(bundle, error)) {
            return false;
        }
        if (loaded != nullptr) {
            *loaded = std::move(bundle);
        }
        return true;
    } catch (const std::exception& ex) {
        if (error != nullptr) {
            *error = ex.what();
        }
        return false;
    }
}

void copyRawMaterial(
    const RawMaterialBundle& bundle,
    const std::filesystem::path& destination) {
    std::filesystem::create_directories(destination);
    for (const auto& file : bundle.files) {
        const auto target = destination / file.relativePath;
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy_file(
            bundle.root / file.relativePath,
            target,
            std::filesystem::copy_options::overwrite_existing);
    }
    std::filesystem::copy_file(
        bundle.root / "MANIFEST.json",
        destination / "MANIFEST.json",
        std::filesystem::copy_options::overwrite_existing);
}

}  // namespace clipfix
