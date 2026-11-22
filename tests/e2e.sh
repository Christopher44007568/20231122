#!/usr/bin/env bash
set -euo pipefail

binary="${1:-build/gbuffer-inspector}"
workspace_root="${GBI_TEST_WORKSPACE_ROOT:-${TMPDIR:-${PWD}}}"
workspace="${2:-$(mktemp -d "${workspace_root}/gbi-e2e.XXXXXX")}"

if [[ ! -x "${binary}" ]]; then
  echo "binary not executable: ${binary}" >&2
  exit 2
fi

"${binary}" init --workspace "${workspace}"
original_hash="$(sha256sum "${workspace}/originals/scene.json" | cut -d' ' -f1)"

"${binary}" capture --workspace "${workspace}" --anchor A0 \
  --width 320 --height 180 --note e2e-source >/dev/null
"${binary}" correct --workspace "${workspace}" --object cube \
  --albedo '#3D8CEB' --roughness 0.32 >/dev/null
"${binary}" capture --workspace "${workspace}" --anchor A1 --object cube \
  --width 320 --height 180 --revision e2e-fix --note e2e-isolate >/dev/null
"${binary}" capture --workspace "${workspace}" --anchor A2 \
  --width 320 --height 180 --revision e2e-accept --note e2e-accept >/dev/null

compare_output="$("${binary}" compare --workspace "${workspace}" \
  --object cube --from A0 --to A1)"
grep -q 'verdict=CHANGED' <<<"${compare_output}"
export_output="$("${binary}" export --workspace "${workspace}" \
  --object cube --from A0 --to A2)"
export_dir="$(sed -n 's/^export=//p' <<<"${export_output}")"

test -f "${export_dir}/acceptance.md"
test -f "${export_dir}/scene-evidence.md"
test -f "${export_dir}/SHA256SUMS"
grep -q 'Source object `cube`' "${export_dir}/acceptance.md"
grep -Eq 'offset `[0-9]+`' "${export_dir}/acceptance.md"
grep -Eq '`[0-9]{4}-[0-9]{2}-[0-9]{2}T' "${export_dir}/acceptance.md"
grep -Eq '^\| `albedo` \| [0-9]+ \| [0-9]+' "${export_dir}/acceptance.md"

"${binary}" verify --workspace "${workspace}" | grep -q 'status=PASS'
test "$(stat -c '%a' "${workspace}/originals/scene.json")" = "444"
test "$(sha256sum "${workspace}/originals/scene.json" | cut -d' ' -f1)" = "${original_hash}"

a0_before="$("${binary}" status --workspace "${workspace}" |
  sed -n 's/^anchor.A0.capture=//p')"
a0_output="$("${binary}" capture --workspace "${workspace}" --anchor A0 \
  --width 320 --height 180 --revision e2e-a0-after-overlay \
  --note a0-must-ignore-overlays)"
a0_after="$(sed -n 's/^capture_id=//p' <<<"${a0_output}")"
grep -q 'value=e83d2947' <<<"${a0_output}"
"${binary}" compare --workspace "${workspace}" --object cube \
  --left-capture "${a0_before}" --right-capture "${a0_after}" \
  --from A0 --to A0 | grep -q 'verdict=IDENTICAL'

resume_workspace="${workspace}-resume"
"${binary}" init --workspace "${resume_workspace}" >/dev/null
if GBI_TEST_ABORT_AFTER_VIEW=2 "${binary}" capture \
  --workspace "${resume_workspace}" --anchor A0 \
  --width 320 --height 180 --note e2e-interrupt >/dev/null 2>&1; then
  echo "expected simulated interruption" >&2
  exit 1
fi
"${binary}" resume --workspace "${resume_workspace}" >/dev/null
"${binary}" verify --workspace "${resume_workspace}" | grep -q 'status=PASS'

crash_workspace="${workspace}-crash"
"${binary}" init --workspace "${crash_workspace}" >/dev/null
set +e
GBI_TEST_CRASH_AFTER_VIEW=2 "${binary}" capture \
  --workspace "${crash_workspace}" --anchor A0 \
  --width 320 --height 180 --note e2e-hard-crash >/dev/null 2>&1
crash_status=$?
set -e
test "${crash_status}" = "137"
"${binary}" status --workspace "${crash_workspace}" >/dev/null
"${binary}" resume --workspace "${crash_workspace}" >/dev/null
"${binary}" verify --workspace "${crash_workspace}" | grep -q 'status=PASS'

echo "e2e PASS"
echo "workspace=${workspace}"
echo "export=${export_dir}"
