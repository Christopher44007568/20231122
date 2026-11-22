#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${root}"

if [[ -z "${VK_ICD_FILENAMES:-}" && -f /usr/share/vulkan/icd.d/lvp_icd.json ]]; then
  export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
fi

./scripts/build.sh
./build/msphere_energy --self-test --out evidence/acceptance
mkdir -p docs
cp evidence/acceptance/acceptance.md docs/ACCEPTANCE.md
(cd materials && sha256sum -c original.sha256)
sha256sum materials/original.material evidence/acceptance/original/original.material
cmp materials/original.material evidence/acceptance/original/original.material
