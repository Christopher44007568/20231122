#include "accept.h"
#include "core/generator.h"
#include "core/material.h"
#include "core/util.h"
#include "runner.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

void printUsage() {
    std::printf(
        "ssor_reconstruct - screen-space outline forensic harness\n"
        "\n"
        "usage:\n"
        "  ssor run [--material FILE] [--out DIR]\n"
        "  ssor inspect [--material FILE]\n"
        "  ssor accept [--material FILE] [--out DIR]\n"
        "  ssor make-material [--out RAW_DIR] [--pristine DIR]\n"
        "  ssor version\n"
        "\n"
        "exit codes:\n"
        "  0 success, 2 missing input, 4 empty input, 5 oversized input,\n"
        "  6 Vulkan/render failure, 7 invalid material, 1 acceptance failure\n");
}

int inspectMaterial(const std::string& path) {
    const ssor::ParseResult result = ssor::parseMaterialFile(path);
    nlohmann::ordered_json json;
    if (result.ok) {
        json = ssor::materialToJson(result.material);
        json["status"] = "OK";
        std::printf("%s\n", json.dump(2).c_str());
        return 0;
    }
    json["status"] = "ERROR";
    json["path"] = path;
    json["error"] = {
        {"code", result.code},
        {"message", result.message},
        {"rejectedAtOffset", result.rejectedAtOffset},
        {"observed", result.observed},
        {"limit", result.limit},
    };
    std::printf("%s\n", json.dump(2).c_str());
    return ssor::exitCodeForMaterialError(result.error);
}

int makeMaterial(const std::string& rawDir, const std::string& pristineDir) {
    if (!ssor::writeCanonicalMaterials(rawDir)) {
        std::fprintf(stderr, "cannot write materials to %s\n", rawDir.c_str());
        return 1;
    }
    ssor::makeDirs(pristineDir);
    for (const char* name : {"outline_repeat.ssoraw", "empty_boundaries.ssoraw"}) {
        const std::string src = ssor::pathJoin(rawDir, name);
        const std::string dst = ssor::pathJoin(pristineDir, name);
        if (!ssor::copyFileReadOnly(src, dst)) {
            std::fprintf(stderr, "cannot preserve pristine copy %s\n",
                         dst.c_str());
            return 1;
        }
    }
    const std::string emptyRaw = ssor::pathJoin(rawDir, "empty.ssoraw");
    const std::string emptyPristine =
        ssor::pathJoin(pristineDir, "empty.ssoraw");
    ssor::writeFile(emptyRaw, nullptr, 0);
    ssor::writeFile(emptyPristine, nullptr, 0);
    ::chmod(emptyRaw.c_str(), 0444);
    ::chmod(emptyPristine.c_str(), 0444);
    std::printf("canonical material: %s\n",
                ssor::pathJoin(rawDir, "outline_repeat.ssoraw").c_str());
    std::printf("sha256: %s\n",
                ssor::sha256File(ssor::pathJoin(
                                     rawDir, "outline_repeat.ssoraw"))
                    .c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string command = "run";
    std::string material = "materials/raw/outline_repeat.ssoraw";
    std::string outDir;
    std::string rawDir = "materials/raw";
    std::string pristineDir = "materials/original";
    int index = 1;
    if (index < argc && argv[index][0] != '-') {
        command = argv[index++];
    }
    for (; index < argc; ++index) {
        const std::string arg = argv[index];
        auto next = [&](std::string* target) {
            if (index + 1 < argc) {
                *target = argv[++index];
                return true;
            }
            return false;
        };
        if (arg == "--material") {
            if (!next(&material)) {
                std::fprintf(stderr, "--material needs a value\n");
                return 2;
            }
        } else if (arg == "--out") {
            if (!next(&outDir)) {
                std::fprintf(stderr, "--out needs a value\n");
                return 2;
            }
        } else if (arg == "--pristine") {
            if (!next(&pristineDir)) {
                std::fprintf(stderr, "--pristine needs a value\n");
                return 2;
            }
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            printUsage();
            return 2;
        }
    }

    if (command == "run") {
        ssor::RunOptions options;
        options.materialPath = material;
        options.outDir = outDir;
        const ssor::RunResult result = ssor::runMaterial(options);
        if (result.ok) {
            const auto& report = result.report;
            std::printf("run=OK out=%s\n", result.outDir.c_str());
            std::printf("material=%s sha256=%s\n",
                        result.materialPath.c_str(),
                        report["material"]["sha256"].get<std::string>().c_str());
            std::printf("diffPixels=%llu verdict=%s\n",
                        static_cast<unsigned long long>(
                            report["correlation"]["imageDifference"]
                                  ["diffPixels"]
                                      .get<std::uint64_t>()),
                        report["correlation"]["verdict"]
                            .get<std::string>()
                            .c_str());
            std::printf("evidence=%s\n",
                        ssor::pathJoin(result.outDir, "evidence.jsonl").c_str());
        } else {
            std::fprintf(stderr, "run=ERROR code=%d out=%s: %s\n",
                         result.exitCode, result.outDir.c_str(),
                         result.error.c_str());
        }
        return result.exitCode;
    }
    if (command == "inspect") return inspectMaterial(material);
    if (command == "make-material") return makeMaterial(rawDir, pristineDir);
    if (command == "accept") {
        ssor::AcceptOptions options;
        options.materialPath = material;
        if (!outDir.empty()) options.outDir = outDir;
        return ssor::runAcceptance(options);
    }
    if (command == "version") {
        std::printf("ssor_reconstruct 1.0 (C++17/Vulkan/GLFW)\n");
        return 0;
    }
    if (command == "help") {
        printUsage();
        return 0;
    }
    std::fprintf(stderr, "unknown command: %s\n", command.c_str());
    printUsage();
    return 2;
}
