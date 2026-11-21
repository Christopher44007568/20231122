#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-${ROOT}/build/curved_trim}"
WORKSPACE="$(mktemp -d /tmp/clipfix-flow.XXXXXX)"
REPORT="$(mktemp -d /tmp/clipfix-report.XXXXXX)"

raw_tree_hash() {
    find "${ROOT}/materials/raw" -type f -print0 \
        | sort -z \
        | xargs -0 sha256sum \
        | sha256sum \
        | awk '{print $1}'
}

BEFORE_RAW="$(raw_tree_hash)"

"${TARGET}" init --workspace "${WORKSPACE}" >/dev/null
"${TARGET}" run --workspace "${WORKSPACE}" --object seg01_ridge \
    --offsets "-0.400000,0.200000,-0.100000" >/dev/null
"${TARGET}" checkpoint --workspace "${WORKSPACE}" --reason "automated_mid_exit" >/dev/null
"${TARGET}" status --workspace "${WORKSPACE}" --json > "${REPORT}/status_after_resume.json"

"${TARGET}" run --workspace "${WORKSPACE}" --object seg01_ridge \
    --offsets "-0.300000,0.220000,-0.080000" >/dev/null
for object in seg02_saddle seg03_apron; do
    "${TARGET}" run --workspace "${WORKSPACE}" --object "${object}" >/dev/null
    "${TARGET}" run --workspace "${WORKSPACE}" --object "${object}" >/dev/null
done

for object in seg01_ridge seg02_saddle seg03_apron; do
    "${TARGET}" compare --workspace "${WORKSPACE}" --object "${object}" >/dev/null
    "${TARGET}" export --workspace "${WORKSPACE}" --object "${object}" >/dev/null
done

"${TARGET}" compare --workspace "${WORKSPACE}" --object seg01_ridge --previous >/dev/null
"${TARGET}" accept --workspace "${WORKSPACE}" --out "${REPORT}" --no-fill >/dev/null

AFTER_RAW="$(raw_tree_hash)"
if [[ "${BEFORE_RAW}" != "${AFTER_RAW}" ]]; then
    echo "raw material changed: ${BEFORE_RAW} -> ${AFTER_RAW}" >&2
    exit 1
fi

export REPORT WORKSPACE BEFORE_RAW AFTER_RAW
python3 - <<'PY'
import json
import os
import pathlib
import re

report_dir = pathlib.Path(os.environ["REPORT"])
workspace = pathlib.Path(os.environ["WORKSPACE"])
report = json.loads((report_dir / "acceptance_report.json").read_text())
status = json.loads((report_dir / "status_after_resume.json").read_text())

assert report["result"] == "PASS", report.get("failures")
assert report["raw_material"]["source_matches_copy"] is True
assert report["raw_material"]["source_verified"] is True
assert report["raw_material"]["copy_verified"] is True
assert len(report["anchors"]) == 3
assert [a["raw_offset_text"] for a in report["anchors"]] == [
    "-0.350000", "0.180000", "-0.120000"
]
assert status["resume_count"] >= 1, status
assert status["last_suspend_at"]
assert status["last_resume_at"]
assert status["verification"]["raw_material_ok"] is True
assert status["verification"]["evidence_chain_ok"] is True

stamp = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}(Z|[+-]\d{4})$")
for obj in report["objects"]:
    assert obj["run_count"] >= 2, obj
    assert obj["pass"] is True, obj
    assert len(obj["runs"]) == 2
    assert len({run["run_id"] for run in obj["runs"]}) == 2
    for run in obj["runs"]:
        assert stamp.match(run["started_utc"])
        assert stamp.match(run["finished_utc"])
        assert run["finished_epoch_ms"] >= run["started_epoch_ms"]
        assert run["raw_offsets_text"] == ["-0.350000", "0.180000", "-0.120000"]
        assert run["affected_count"] > 0
        assert len(run["output_obj_sha256"]) == 64
        assert len(run["evidence_csv_sha256"]) == 64
        assert (workspace / run["output_obj"]).is_file()
        assert (workspace / run["evidence_csv"]).is_file()
    assert obj["compare"]["changed_vertices"] > 0
    assert stamp.match(obj["compare"]["created_utc"])
    assert obj["export"]["obj_sha256"]
    assert (workspace / obj["export"]["obj_path"]).is_file()
    assert (workspace / obj["export"]["report_path"]).is_file()

check_names = {check["name"] for check in report["checks"]}
for required in [
    "raw_material_unmodified",
    "three_anchors_present",
    "three_render_objects_present",
    "raw_offsets_exact",
    "evidence_chain_valid",
    "mid_exit_and_reentry_evidenced",
]:
    assert required in check_names, required

original = workspace / "original_material"
for path in original.rglob("*"):
    if path.is_file():
        assert path.stat().st_mode & 0o222 == 0, f"raw copy is writable: {path}"

timeline = (report_dir / "evidence_timeline.csv").read_text().splitlines()
assert len(timeline) > 10
assert any("session_suspended" in line for line in timeline)
assert any("session_resumed" in line for line in timeline)

session = json.loads((workspace / "session.json").read_text())
assert session["suspended"] is False
assert session["resume_count"] >= 1
print("acceptance flow: PASS")
print("workspace:", workspace)
print("report:", report_dir)
PY
