#!/usr/bin/env bash
# Reproducible end-to-end review run.
# Builds (if needed), regenerates fixtures, and runs the two-snapshot GPU
# skinning review against sample_input/, writing evidence to review_output/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

# Headless CPU Vulkan (lavapipe). Override ICD by exporting VK_ICD_FILENAMES.
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}"

if [[ ! -x third_party/glslang/install/bin/glslangValidator ]]; then
    ./setup_dependencies.sh
fi

if [[ ! -x build/skinning_review ]]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi
cmake --build build -j"$(nproc)"

build/generate_sample sample_input
build/skinning_review sample_input review_output

echo
echo "Evidence tree:"
find review_output -type f | sort
