#include "format.hpp"

#include <fstream>

namespace {

using inst::FormatRevision;
using inst::Submission;

Submission make(uint32_t objectId, uint32_t revision, uint32_t selector,
                uint32_t model, float x, float y, float scale,
                float r, float g, float b) {
    Submission item;
    item.objectId = objectId;
    item.revision = revision;
    item.legacySelector = selector;
    item.model = model;
    item.x = x;
    item.y = y;
    item.z = 0.0f;
    item.scale = scale;
    item.r = r;
    item.g = g;
    item.b = b;
    item.yaw = 0.18f * static_cast<float>(objectId % 3);
    return item;
}

} // namespace

int main(int argc, char** argv) {
    const inst::fs::path directory =
        argc > 1 ? inst::fs::path(argv[1]) : inst::fs::path("sample_input");
    inst::fs::create_directories(directory);

    const std::vector<Submission> current = {
        make(1, 1, 0, 1, -0.78f, 0.08f, 0.54f, 0.92f, 0.32f, 0.22f),
        make(2, 1, 1, 0, 0.00f, 0.08f, 0.56f, 0.25f, 0.78f, 0.34f),
        make(2, 2, 0, 2, 0.00f, 0.08f, 0.92f, 0.24f, 0.82f, 0.86f),
        make(3, 1, 2, 2, 0.82f, 0.08f, 0.78f, 0.30f, 0.44f, 0.95f),
        make(4, 1, 1, 1, 0.00f, -0.58f, 0.34f, 0.95f, 0.78f, 0.22f),
    };
    inst::writeInputObject(directory / "control_current.imd2",
                           FormatRevision::CurrentV2, 1001, current);

    const std::vector<Submission> legacy = {
        make(5, 1, 2, 2, 0.42f, 0.58f, 0.38f, 0.72f, 0.38f, 0.92f),
        make(6, 1, 1, 1, -0.58f, 0.58f, 0.38f, 0.34f, 0.84f, 0.72f),
    };
    inst::writeInputObject(directory / "aux_legacy.imd1",
                           FormatRevision::LegacyV1, 1002, legacy);

    const std::vector<Submission> retry = {
        make(2, 2, 0, 2, 0.00f, 0.08f, 0.92f, 0.24f, 0.82f, 0.86f),
        make(1, 1, 0, 1, -0.78f, 0.08f, 0.54f, 0.92f, 0.32f, 0.22f),
    };
    inst::writeInputObject(directory / "duplicate_retry.imd2",
                           FormatRevision::CurrentV2, 1003, retry);

    const std::vector<Submission> after = {
        make(7, 1, 0, 0, 0.08f, 0.56f, 0.26f, 0.92f, 0.48f, 0.66f),
    };
    inst::writeInputObject(directory / "zz_after_poison.any",
                           FormatRevision::CurrentV2, 1004, after);

    const inst::fs::path poison = directory / "mystery_payload.qzv";
    std::ofstream unknown(poison, std::ios::binary);
    const uint32_t unknownMagic = 0x58444D49u; // "IMDX"
    const uint16_t version = 9;
    const uint16_t headerBytes = 32;
    const uint32_t zero = 0;
    const uint64_t zero64 = 0;
    unknown.write(reinterpret_cast<const char*>(&unknownMagic),
                  sizeof(unknownMagic));
    unknown.write(reinterpret_cast<const char*>(&version), sizeof(version));
    unknown.write(reinterpret_cast<const char*>(&headerBytes),
                  sizeof(headerBytes));
    unknown.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
    unknown.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
    unknown.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
    unknown.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
    unknown.write(reinterpret_cast<const char*>(&zero64), sizeof(zero64));
    unknown.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
    unknown.write("UNKNOWN-EXTENSION-FIXTURE", 23);

    std::printf("fixtures written to %s\n", directory.string().c_str());
    return 0;
}
