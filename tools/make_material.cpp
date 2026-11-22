#include "core/generator.h"
#include "core/material.h"
#include "core/util.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    std::string outDir = "materials/raw";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            outDir = argv[++i];
        } else {
            std::fprintf(stderr, "usage: ssor_make_material [--out DIR]\n");
            return 2;
        }
    }
    if (!ssor::writeCanonicalMaterials(outDir)) {
        std::fprintf(stderr, "failed to write materials to %s\n", outDir.c_str());
        return 1;
    }
    const ssor::RawMaterial canonical = ssor::makeCanonicalMaterial();
    const std::string path =
        ssor::pathJoin(outDir, "outline_repeat.ssoraw");
    std::printf("wrote %s bytes=%llu objects=%u probes=%u sha256=%s\n",
                path.c_str(),
                static_cast<unsigned long long>(ssor::fileSize(path)),
                canonical.objectCount, canonical.boundaryCount,
                ssor::sha256File(path).c_str());
    return 0;
}
