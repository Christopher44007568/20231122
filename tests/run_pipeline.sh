#!/usr/bin/env bash
# End-to-end reviewable-pipeline test: record -> replay (clean) ->
# replay recovered/corrupted (diagnosis) -> replay with hypothesis override
# (confirmation) -> replay legacy v1 (format compatibility).
set -u

BIN="$1"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export LD_LIBRARY_PATH="$ROOT/third_party/sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
fail=0

check() {  # check <desc> <expected> <actual>
  if [ "$2" = "$3" ]; then
    echo "PASS: $1"
  else
    echo "FAIL: $1 (expected $2, got $3)"
    fail=1
  fi
}

# 1. Record v2 and legacy v1 captures, plus a "recovered" capture whose
#    draw 1 topology was corrupted during recovery.
"$BIN" record --out "$WORK/good.ptdc" >/dev/null
check "record v2" 0 $?
"$BIN" record --out "$WORK/legacy.ptdc" --legacy >/dev/null
check "record legacy v1" 0 $?
"$BIN" record --out "$WORK/recovered.ptdc" --corrupt-topology 1 TRIANGLE_LIST >/dev/null 2>&1
check "record recovered (corrupt)" 0 $?

# 2. Clean replay: no error-level findings.
"$BIN" replay "$WORK/good.ptdc" --evidence "$WORK/ev_good" >/dev/null
check "replay clean exits 0" 0 $?
grep -q "0 像素差异" "$WORK/ev_good"/*/report.md
check "clean replay reports zero diff" 0 $?
grep -q "未知扩展 VK_EXT_unknown_topology_probe" "$WORK/ev_good"/*/report.md
check "unknown extension recorded" 0 $?

# 3. Recovered capture: diagnosis must find the diff and blame draw 1.
"$BIN" replay "$WORK/recovered.ptdc" --evidence "$WORK/ev_bad" >/dev/null
check "replay recovered exits 2" 2 $?
grep -q "首要怀疑对象: draw 1" "$WORK/ev_bad"/*/report.md
check "diagnosis blames draw 1" 0 $?
test -f "$WORK/ev_bad"/*/diff.ppm
check "diff image preserved as evidence" 0 $?
grep -q "override-topology 1=TRIANGLE_LIST" "$WORK/ev_bad"/*/report.md
check "actionable follow-up suggested" 0 $?

# 4. Hypothesis verification: the suggested override must clear the diff.
"$BIN" replay "$WORK/recovered.ptdc" --evidence "$WORK/ev_fix" --override-topology 1=TRIANGLE_STRIP >/dev/null
check "override replay exits 0" 0 $?
grep -q "0 像素差异" "$WORK/ev_fix"/*/report.md
check "override clears the diff" 0 $?

# 5. Legacy format: hash-only comparison, limitation noted.
"$BIN" replay "$WORK/legacy.ptdc" --evidence "$WORK/ev_legacy" >/dev/null
check "replay legacy exits 0" 0 $?
grep -q "旧格式捕获" "$WORK/ev_legacy"/*/report.md
check "legacy limitation noted" 0 $?

# 6. Report separates automated conclusions from manual confirmation.
grep -q "## 自动结论" "$WORK/ev_bad"/*/report.md && grep -q "## 人工确认" "$WORK/ev_bad"/*/report.md
check "report separates auto/manual sections" 0 $?

if [ "$fail" = 0 ]; then echo "ALL TESTS PASSED"; else echo "TESTS FAILED"; fi
exit $fail
