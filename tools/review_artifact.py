#!/usr/bin/env python3
import hashlib
import json
import sys
from pathlib import Path

CHECKPOINT_FIELDS = [
    "schema", "checkpoint_id", "created_at_utc", "config", "scene", "device_clocks",
    "render_plan", "progress", "frames", "resource_events", "state", "field_order",
]
MANIFEST_FIELDS = [
    "schema", "producer", "run", "config", "scene", "device_clocks", "render_plan",
    "progress", "frames", "resource_events", "resume", "result", "field_order",
]
COMPARISON_FIELDS = ["schema", "reference", "candidate", "frames", "summary", "field_order"]
EVIDENCE_FIELDS = [
    "schema", "generated_at_utc", "producer", "command_line", "run_root",
    "headline_result", "clock_alignment", "runs", "degradation_probes", "artifacts",
    "checks", "reproduction", "field_order",
]
FRAME_FIELDS = [
    "task_index", "sequence", "device_index", "device_id", "estimated_reference_ns",
    "file", "sha256", "bytes",
]
CONFIG_FIELDS = [
    "width", "height", "frames", "pause_after_tasks", "memory_budget_bytes",
    "pressure_budget_bytes", "backend", "seed", "object_uid",
]
PLAN_FIELDS = [
    "requested_backend", "effective_backend", "degraded", "reason", "budget_bytes",
    "full_transient_bytes", "band_height", "band_count",
]


class ReviewError(RuntimeError):
    pass


def load_json(path):
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        raise ReviewError(f"cannot load {path}: {error}") from error


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def check(condition, message, lines):
    lines.append(("PASS " if condition else "FAIL ") + message)
    if not condition:
        raise ReviewError(message)


def check_order(value, expected, label, lines):
    keys = list(value.keys())
    check(keys == expected, f"{label} fields and order", lines)
    check(value.get("field_order") == expected, f"{label} field_order declaration", lines)


def read_ppm(path):
    data = path.read_bytes()
    position = 0
    tokens = []
    while len(tokens) < 4:
        while position < len(data) and data[position] in b" \t\r\n":
            position += 1
        if position < len(data) and data[position] == ord("#"):
            while position < len(data) and data[position] not in b"\r\n":
                position += 1
            continue
        start = position
        while position < len(data) and data[position] not in b" \t\r\n":
            position += 1
        if start == position:
            raise ReviewError(f"truncated PPM header: {path}")
        tokens.append(data[start:position])
    if position < len(data) and data[position] == 13:
        position += 1
        if position < len(data) and data[position] == 10:
            position += 1
    elif position < len(data) and data[position] in b" \t\n":
        position += 1
    magic, width_text, height_text, maximum_text = tokens
    if magic != b"P6" or int(maximum_text) != 255:
        raise ReviewError(f"unsupported PPM format: {path}")
    width, height = int(width_text), int(height_text)
    pixels = data[position:]
    if len(pixels) != width * height * 3:
        raise ReviewError(f"PPM byte count mismatch: {path}")
    return width, height, pixels


def verify_frames(root, manifest, lines, label):
    config = manifest["config"]
    previous_task = -1
    for frame in manifest["frames"]:
        check(list(frame.keys()) == FRAME_FIELDS, f"{label} frame field order", lines)
        task_index = int(frame["task_index"])
        check(task_index == previous_task + 1, f"{label} frame {task_index} order", lines)
        previous_task = task_index
        path = root / frame["file"]
        check(path.is_file(), f"{label} frame file exists: {frame['file']}", lines)
        check(path.stat().st_size == int(frame["bytes"]), f"{label} frame size: {frame['file']}", lines)
        check(sha256(path) == frame["sha256"], f"{label} frame SHA-256: {frame['file']}", lines)
        width, height, _ = read_ppm(path)
        check((width, height) == (int(config["width"]), int(config["height"])),
              f"{label} frame dimensions: {frame['file']}", lines)
    check(len(manifest["frames"]) > 0, f"{label} has frames", lines)


def verify(root):
    lines = []
    evidence = load_json(root / "evidence.json")
    check_order(evidence, EVIDENCE_FIELDS, "evidence", lines)
    check(evidence["schema"] == "bokeh.evidence.v1", "evidence schema", lines)

    for artifact in evidence["artifacts"]:
        relative = Path(artifact["path"])
        check(not relative.is_absolute() and ".." not in relative.parts,
              f"artifact path stays inside root: {relative}", lines)
        path = root / relative
        check(path.is_file(), f"artifact exists: {relative}", lines)
        check(path.stat().st_size == int(artifact["bytes"]), f"artifact size: {relative}", lines)
        check(sha256(path) == artifact["sha256"], f"artifact SHA-256: {relative}", lines)

    baseline = load_json(root / "baseline" / "manifest.json")
    resumed = load_json(root / "resumed" / "manifest.json")
    check_order(baseline, MANIFEST_FIELDS, "baseline manifest", lines)
    check_order(resumed, MANIFEST_FIELDS, "resumed manifest", lines)
    check(list(baseline["config"].keys()) == CONFIG_FIELDS, "baseline config field order", lines)
    check(list(resumed["config"].keys()) == CONFIG_FIELDS, "resumed config field order", lines)
    check(list(baseline["render_plan"].keys()) == PLAN_FIELDS,
          "baseline render plan field order", lines)
    check(list(resumed["render_plan"].keys()) == PLAN_FIELDS,
          "resumed render plan field order", lines)
    check(baseline["config"] == resumed["config"], "baseline/resumed config match", lines)
    verify_frames(root / "baseline", baseline, lines, "baseline")
    verify_frames(root / "resumed", resumed, lines, "resumed")

    checkpoint = load_json(root / "paused" / "checkpoint.json")
    check_order(checkpoint, CHECKPOINT_FIELDS, "checkpoint", lines)
    check(list(checkpoint["config"].keys()) == CONFIG_FIELDS, "checkpoint config field order", lines)
    check(list(checkpoint["render_plan"].keys()) == PLAN_FIELDS,
          "checkpoint render plan field order", lines)
    next_task = int(checkpoint["progress"]["next_task_index"])
    check(0 < next_task < int(checkpoint["progress"]["total_tasks"]),
          "checkpoint has a resume boundary", lines)
    check(len(checkpoint["frames"]) == next_task, "checkpoint frame count", lines)
    checkpoint_hashes = []
    for frame in checkpoint["frames"]:
        check(list(frame.keys()) == FRAME_FIELDS, "checkpoint frame field order", lines)
        path = root / "paused" / frame["file"]
        check(path.is_file() and sha256(path) == frame["sha256"],
              f"checkpoint frame SHA-256: {frame['file']}", lines)
        checkpoint_hashes.append((frame["task_index"], frame["file"], frame["sha256"]))

    resume = resumed["resume"]
    check(resume["used"] is True, "resumed manifest marks checkpoint use", lines)
    check(resume["checkpoint_sha256"] == sha256(root / "paused" / "checkpoint.json"),
          "resumed manifest checkpoint SHA-256", lines)
    check(int(resume["resumed_from_task"]) == next_task, "resume boundary matches checkpoint", lines)
    for index, (task_index, file_name, frame_hash) in enumerate(checkpoint_hashes):
        frame = resumed["frames"][index]
        check((frame["task_index"], frame["file"], frame["sha256"]) ==
              (task_index, file_name, frame_hash),
              f"resumed prefix preserves frame {task_index} and order", lines)

    comparison = load_json(root / "comparison.json")
    check_order(comparison, COMPARISON_FIELDS, "comparison", lines)
    summary = comparison["summary"]
    check(summary["bitwise_equal"] is True, "comparison is bitwise equal", lines)
    check(int(summary["total_frames"]) == len(baseline["frames"]), "comparison frame count", lines)
    check(int(summary["equal_frames"]) == len(baseline["frames"]), "all comparison frames equal", lines)
    check(summary["final_reference_sha256"] == baseline["frames"][-1]["sha256"],
          "comparison baseline final hash", lines)
    check(summary["final_candidate_sha256"] == resumed["frames"][-1]["sha256"],
          "comparison resumed final hash", lines)
    for entry in comparison["frames"]:
        check(entry["bitwise_equal"] is True, f"comparison frame {entry['task_index']}", lines)
        reference_path = root / "baseline" / entry["reference_file"]
        candidate_path = root / "resumed" / entry["candidate_file"]
        _, _, reference_pixels = read_ppm(reference_path)
        _, _, candidate_pixels = read_ppm(candidate_path)
        check(reference_pixels == candidate_pixels,
              f"pixel bytes match for task {entry['task_index']}", lines)

    alignment = evidence["clock_alignment"]
    check(int(alignment["beta_minus_alpha_offset_ns"]) != 0, "device clocks have an offset", lines)
    check(abs(float(alignment["beta_minus_alpha_drift_ppm"])) > 0.001,
          "device clocks have drift", lines)
    check(alignment["object_uid"] == "obj-20231122-bokeh",
          "repeated object identity is recorded", lines)
    check(int(alignment["repeated_instances"]) > 1, "object is repeated", lines)

    check(all(item["passed"] is True for item in evidence["checks"]),
          "all embedded evidence checks pass", lines)
    check(evidence["headline_result"]["status"] == "PASS", "headline status", lines)
    probes = evidence["degradation_probes"]
    degraded = [probe for probe in probes if probe["degraded"] is True]
    backends = {probe["backend"] for probe in degraded}
    check(len(degraded) >= 2 and len(backends) >= 2,
          "banded Vulkan and CPU fallback evidence", lines)
    for probe in degraded:
        check(len(probe["resource_events"]) > 0,
              f"degradation probe records resource events: {probe['backend']}", lines)

    return lines


def main():
    if len(sys.argv) != 2:
        print("usage: review_artifact.py ARTIFACT_DIR", file=sys.stderr)
        return 64
    root = Path(sys.argv[1]).resolve()
    if not root.is_dir():
        print(f"FAIL not an artifact directory: {root}", file=sys.stderr)
        return 66
    try:
        lines = verify(root)
    except (ReviewError, KeyError, ValueError, OSError) as error:
        print(f"FAIL {error}", file=sys.stderr)
        print("REVIEW FAIL")
        return 2
    for line in lines:
        print(line)
    print("REVIEW PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())

