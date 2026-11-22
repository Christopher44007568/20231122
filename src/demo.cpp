#include "cache_format.hpp"

#include "hash.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace lshc {
namespace {

struct DemoObject {
    uint32_t objectId;
    float centerX;
    float centerY;
    float radius;
    float lightX;
    float lightY;
    float lightZ;
    float strength;
    float penumbra;
    uint32_t resourceRevision;
};

void appendU16(std::vector<uint8_t>& bytes, uint16_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8u));
}

void appendU32(std::vector<uint8_t>& bytes, uint32_t value)
{
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        bytes.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void appendU64(std::vector<uint8_t>& bytes, uint64_t value)
{
    for (unsigned shift = 0; shift < 64u; shift += 8u) {
        bytes.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void setU32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value)
{
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        bytes[offset + shift / 8u] = static_cast<uint8_t>(value >> shift);
    }
}

void setU64(std::vector<uint8_t>& bytes, std::size_t offset, uint64_t value)
{
    for (unsigned shift = 0; shift < 64u; shift += 8u) {
        bytes[offset + shift / 8u] = static_cast<uint8_t>(value >> shift);
    }
}

uint32_t floatBits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::vector<uint8_t> makeRecord(const DemoObject& object, uint32_t drawCall,
                                uint32_t pipelineId, bool current)
{
    std::vector<uint8_t> record(48u, 0u);
    setU32(record, 0u, object.objectId);
    setU32(record, 4u, drawCall);
    setU32(record, 8u, floatBits(object.centerX));
    setU32(record, 12u, floatBits(object.centerY));
    setU32(record, 16u, floatBits(object.radius));
    setU32(record, 20u, floatBits(object.lightX));
    setU32(record, 24u, floatBits(object.lightY));
    setU32(record, 28u, floatBits(object.lightZ));
    setU32(record, 32u, floatBits(object.strength));
    setU32(record, 36u, floatBits(object.penumbra));
    setU32(record, 40u, object.resourceRevision);
    setU32(record, 44u, crc32c(record.data(), 44u));

    if (!current) {
        return record;
    }
    record.resize(64u, 0u);
    setU32(record, 48u, pipelineId);
    setU32(record, 52u, 0u);
    setU64(record, 56u, fnv1a64(record.data(), 56u));
    return record;
}

void writeFile(const std::filesystem::path& path,
               const std::vector<uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write fixture: " + path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("failed writing fixture: " + path.string());
    }
}

std::vector<uint8_t> legacySnapshot(const std::vector<DemoObject>& objects)
{
    std::vector<uint8_t> bytes{'L', 'S', 'H', 'C'};
    appendU16(bytes, 1u);
    appendU16(bytes, 16u);
    appendU32(bytes, static_cast<uint32_t>(objects.size()));
    appendU32(bytes, 48u);
    for (const DemoObject& object : objects) {
        const auto record =
            makeRecord(object, object.objectId, 0u, false);
        bytes.insert(bytes.end(), record.begin(), record.end());
    }
    return bytes;
}

std::vector<uint8_t> currentSnapshot(const std::vector<DemoObject>& objects,
                                     const DemoObject& truncated)
{
    std::vector<uint8_t> bytes{'L', 'S', 'H', 'C'};
    appendU16(bytes, 2u);
    appendU16(bytes, 24u);
    appendU32(bytes, static_cast<uint32_t>(objects.size() + 1u));
    appendU32(bytes, 64u);
    appendU64(bytes, 0x2023112200020002ull);
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const DemoObject& object = objects[index];
        const uint32_t drawCall = object.objectId == 3u ? 9u : object.objectId;
        const auto record = makeRecord(object, drawCall,
                                       200u + object.objectId, true);
        bytes.insert(bytes.end(), record.begin(), record.end());
    }

    const auto sixth = makeRecord(truncated, 6u, 206u, true);
    bytes.insert(bytes.end(), sixth.begin(), sixth.begin() + 32);
    return bytes;
}

}  // namespace

DemoFiles writeDemoSnapshots(const std::filesystem::path& directory)
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        throw std::runtime_error("cannot create fixture directory: " +
                                 directory.string() + ": " + error.message());
    }

    std::vector<DemoObject> oldObjects = {
        {1u, 0.00f, 0.00f, 0.42f, -0.40f, 0.50f, 0.80f, 0.48f, 0.012f, 11u},
        {2u, -0.25f, -0.10f, 0.30f, 0.60f, 0.30f, 0.70f, 0.50f, 0.010f, 22u},
        {3u, 0.30f, 0.12f, 0.24f, -0.90f, 0.20f, 0.40f, 0.55f, 0.008f, 33u},
        {4u, 0.05f, -0.32f, 0.18f, 0.30f, -0.70f, 0.65f, 0.40f, 0.015f, 44u},
        {5u, -0.42f, 0.36f, 0.20f, -0.20f, 0.80f, 0.55f, 0.60f, 0.020f, 55u},
        {6u, 0.45f, -0.35f, 0.16f, 0.70f, 0.10f, 0.70f, 0.35f, 0.006f, 66u},
    };
    std::vector<DemoObject> newObjects = oldObjects;
    newObjects[1].strength = 0.72f;
    newObjects[2].lightX = -0.45f;
    newObjects[2].strength = 0.72f;
    newObjects[2].penumbra = 0.020f;
    newObjects[3].radius = std::numeric_limits<float>::quiet_NaN();
    newObjects.pop_back();

    DemoObject truncated = oldObjects.back();
    truncated.strength = 0.36f;

    DemoFiles files;
    files.legacyPath = directory / "snapshot-v1.4.2-legacy.cache";
    files.currentPath = directory / "snapshot-v2.0.0-current.cache";
    writeFile(files.legacyPath, legacySnapshot(oldObjects));
    writeFile(files.currentPath, currentSnapshot(newObjects, truncated));
    return files;
}

}  // namespace lshc
