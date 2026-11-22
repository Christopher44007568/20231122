#include "core/generator.h"
#include "core/material.h"
#include "core/scene.h"
#include "core/sha256.h"
#include "core/util.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        ++failures;
    }
}

std::string tempRoot() {
    const std::string path =
        (std::filesystem::temp_directory_path() /
         ("ssor-tests-" + std::to_string(ssor::epochMillis())))
            .string();
    std::filesystem::create_directories(path);
    return path;
}

void testSha256() {
    expect(ssor::sha256Hex("abc") ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "SHA-256 known vector");
    expect(ssor::sha256Hex("") ==
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
           "SHA-256 empty vector");
}

void testCanonicalMaterial() {
    const ssor::RawMaterial material = ssor::makeCanonicalMaterial();
    const std::vector<std::uint8_t> bytes = ssor::encodeMaterial(material);
    expect(bytes.size() == 880, "canonical material is 880 bytes");
    expect(material.objects.size() == 3, "canonical material has 3 objects");
    expect(material.boundaryPixels.size() == 35,
           "canonical material has 35 probes");
    expect(material.duplicateObjectIndex() == 2,
           "duplicate object is objects[2]");
    expect(material.objects[2].repeatOf == 1,
           "duplicate repeats objects[1]");
    expect(std::fabs(material.objects[2].originalOffset.x - 0.58f) < 1e-6f,
           "duplicate original offset x");

    const std::string root = tempRoot();
    const std::string path = ssor::pathJoin(root, "canonical.ssoraw");
    expect(ssor::writeMaterialFile(path, material), "write canonical material");
    const ssor::ParseResult parsed = ssor::parseMaterialFile(path);
    expect(parsed.ok, "parse canonical material");
    if (parsed.ok) {
        expect(parsed.material.objectCount == 3, "parsed objectCount");
        expect(parsed.material.boundaryCount == 35, "parsed boundaryCount");
        expect(parsed.material.outlineDepthOffset == 32, "depth offset");
        expect(parsed.material.normalThresholdOffset == 36, "normal offset");
        expect(parsed.material.boundaryCountOffset == 28, "boundary count offset");
        expect(parsed.material.objectsOffset == 80, "objects base");
        expect(parsed.material.boundariesOffset == 320, "boundaries base");
        expect(parsed.material.objects[2].originalOffsetOffset == 284,
               "duplicate originalOffset offset");
        expect(parsed.material.boundaryPixels[0].recordOffset == 320,
               "first boundary source offset");
        expect(parsed.material.sha256 == ssor::sha256File(path),
               "material SHA-256");
    }
    std::filesystem::remove_all(root);
}

void testSceneBuild() {
    const ssor::RawMaterial material = ssor::makeCanonicalMaterial();
    const ssor::SceneBuild scenes = ssor::buildScenes(material);
    expect(scenes.baseline.draws.size() == 2, "baseline has 2 object draws");
    expect(scenes.repeat.draws.size() == 3, "repeat has 3 object draws");
    expect(scenes.repeat.draws.back().duplicate, "repeat last draw is duplicate");
    expect(scenes.repeat.draws.back().objectIdValue == 3,
           "duplicate objectIdValue is 3");
    expect(std::fabs(scenes.repeat.draws.back().model.m[12] - 0.22f) < 1e-6f,
           "duplicate translation x");
}

void testErrors() {
    const std::string root = tempRoot();
    const std::string empty = ssor::pathJoin(root, "empty.ssoraw");
    ssor::writeFile(empty, nullptr, 0);
    const ssor::ParseResult emptyResult = ssor::parseMaterialFile(empty);
    expect(!emptyResult.ok &&
               emptyResult.error == ssor::MaterialError::EmptyFile &&
               emptyResult.code == "EMPTY_INPUT" &&
               emptyResult.rejectedAtOffset == 0,
           "empty input rejected at offset 0");

    const std::vector<std::uint8_t> canonical =
        ssor::encodeMaterial(ssor::makeCanonicalMaterial());
    std::vector<std::uint8_t> badMagic = canonical;
    badMagic[0] = 'X';
    const std::string badMagicPath = ssor::pathJoin(root, "bad_magic.ssoraw");
    ssor::writeFile(badMagicPath, badMagic.data(), badMagic.size());
    const ssor::ParseResult magicResult =
        ssor::parseMaterialFile(badMagicPath);
    expect(!magicResult.ok &&
               magicResult.error == ssor::MaterialError::BadMagic,
           "bad magic rejected");

    std::vector<std::uint8_t> hugeCount = canonical;
    hugeCount[28] = 0xff;
    hugeCount[29] = 0xff;
    hugeCount[30] = 0xff;
    hugeCount[31] = 0xff;
    const std::string hugeCountPath =
        ssor::pathJoin(root, "huge_count.ssoraw");
    ssor::writeFile(hugeCountPath, hugeCount.data(), hugeCount.size());
    const ssor::ParseResult hugeResult =
        ssor::parseMaterialFile(hugeCountPath);
    expect(!hugeResult.ok &&
               hugeResult.error ==
                   ssor::MaterialError::OversizedDeclaredCount &&
               hugeResult.rejectedAtOffset == 28 &&
               hugeResult.observed == 0xffffffffull &&
               hugeResult.limit == 65536,
           "oversized boundaryCount rejected at offset 28");

    std::vector<std::uint8_t> truncated = canonical;
    truncated[28] = 100;
    truncated[29] = 0;
    truncated[30] = 0;
    truncated[31] = 0;
    const std::string truncatedPath =
        ssor::pathJoin(root, "truncated.ssoraw");
    ssor::writeFile(truncatedPath, truncated.data(), truncated.size());
    const ssor::ParseResult truncatedResult =
        ssor::parseMaterialFile(truncatedPath);
    expect(!truncatedResult.ok &&
               truncatedResult.error ==
                   ssor::MaterialError::TruncatedPayload,
           "truncated payload rejected");

    const std::string oversized = ssor::pathJoin(root, "oversized.ssoraw");
    {
        std::ofstream out(oversized, std::ios::binary | std::ios::trunc);
        out.put('\0');
    }
    std::filesystem::resize_file(oversized, ssor::ParseLimits{}.maxFileBytes + 1);
    const ssor::ParseResult oversizedResult =
        ssor::parseMaterialFile(oversized);
    expect(!oversizedResult.ok &&
               oversizedResult.error == ssor::MaterialError::OversizedFile &&
               oversizedResult.observed >
                   oversizedResult.limit,
           "oversized file rejected before read");
    std::filesystem::remove_all(root);
}

}  // namespace

int main() {
    testSha256();
    testCanonicalMaterial();
    testSceneBuild();
    testErrors();
    if (failures == 0) std::printf("all core tests passed\n");
    return failures == 0 ? 0 : 1;
}
