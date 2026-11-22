# Design: camera trajectory interpolation local-correction tool

## Requirements to design mapping

| Requirement | Implementation |
| --- | --- |
| Two devices with unsynchronized clocks, one missing segment | `ClockMapping` per device (`offset_seconds`, `scale`) maps device time to a reference timeline; demo data drops every sample inside a gap window |
| Every render object can be re-run, compared and exported independently | All engine operations are keyed by `object_id`; `run`, `compare`, `export` CLI commands operate per object/run |
| Exit and re-enter mid-way | `session.json` stores a `running` state with run id and a hash of the correction config; `resume` reuses the run id when the config matches |
| Retain key evidence | Immutable input snapshots under `evidence/snapshots/<sha256>/`, a manifest of input hashes, and an append-only hash-chained run log |
| Exported records independently reviewable by a colleague | Each export bundles project metadata, run files, input snapshots, a hash manifest and `REVIEW.txt`; verification needs only standard sha-256 tooling |
| Reloaded artifacts keep fields and ordering | A purpose-built ordered JSON parser/serializer preserves member insertion order; duplicate keys are rejected while parsing |
| Fixed stack C++17 / Vulkan / GLFW | Core library is plain C++17 with no third-party dependency; the GUI is Vulkan + GLFW only |

## On-disk project layout

```
project.json
inputs/<object>.json
evidence/
  manifest.json
  runs.log.jsonl
  snapshots/<sha256>/<object>.json
session.json
runs/<run_id>.json
exports/<batch_id>/
```

## Determinism and evidence chain

1. Each input trajectory is serialized canonically and snapshotted into a
   content-addressed directory named by its sha-256 digest.
2. A correction config has a canonical hash (`Engine::canonicalConfigHash`).
3. Every completed run writes a full result file and appends an entry to
   `runs.log.jsonl`. Each entry stores the previous entry hash and its own
   hash over every other field, forming a tamper-evident chain.
4. `verify` recomputes the corrected trajectory from the immutable snapshot
   and compares the canonical serialization against the recorded run file,
   then checks the log chain and the evidence manifest.

Because interpolation is a pure function of the snapshot plus the config,
identical inputs always produce byte-identical run files. This is what makes
mid-way exit/resume safe and exports independently reviewable.

## Interpolation

- Linear: linear position interpolation plus quaternion slerp.
- Cubic: Catmull-Rom position interpolation with edge clamping, slerp for
  orientation.
- Gap output is a fixed-period grid including both endpoints.

## GUI

`camtrak_gui` renders each trajectory as a `LINE_STRIP` through a Vulkan
pipeline. The selected object's missing interval is drawn red and a live
correction preview is drawn green. Keyboard controls:

- `Tab` switch render object
- `M` toggle linear/cubic method
- `R` persist a run through the same engine used by the CLI
- `Esc` close

When no display or Vulkan device is available, the GUI exits with a clear
message; every workflow remains available through the CLI.
