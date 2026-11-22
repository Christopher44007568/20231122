#!/bin/sh
set -eu

cd "$(dirname "$0")"

export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}"

cmake -S . -B build
cmake --build build -j

./build/lshc_review --make-fixtures --fixtures fixtures
./build/lshc_review --self-test --out artifacts/self-test
./build/lshc_review \
    --legacy fixtures/snapshot-v1.4.2-legacy.cache \
    --current fixtures/snapshot-v2.0.0-current.cache \
    --out artifacts/shadow-review

if find artifacts -name '.lshc-tmp-*' -print -quit | grep -q .; then
    echo "temporary review state was not cleaned" >&2
    exit 1
fi

(
    cd artifacts/shadow-review
    sha256sum -c checksums.sha256 >/dev/null
)

echo "validation complete: artifacts/shadow-review/review.md"

