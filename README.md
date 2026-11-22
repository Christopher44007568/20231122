# Pausable Skeletal Keyframe Compression

C++17 + Vulkan compute + GLFW workbench for a deterministic, auditable
keyframe-compression run across two devices whose clocks are not synchronized.

The default demonstration builds a two-device capture, calibrates the clocks,
finds the union coverage hole, pauses halfway through compression, persists the
exact state, resumes it, and compares the resumed result with an uninterrupted
baseline.

## 中文概览

默认演示会生成两台设备的关键帧捕获，对共享动作锚点做仿射时钟校准，检测
两台设备覆盖范围的并集缺口，在压缩中途保存检查点并退出，再从检查点继续。
最终结果与无中断基线做关键帧序列、误差、工作量计数和 SHA-256 全量比较。
低预算时先关闭 Vulkan 暂存区、缩短 chunk、放宽容差，再进入保留全部审计
证据的流式 CPU 路径；不会静默丢弃缺口边界或检查点。

直接运行：

```bash
cmake -S . -B build -G Ninja && cmake --build build
./build/skeleton-kf demo --out artifacts/demo --budget-mb 24 --pause-after-chunks 3
```

产物中的 `review-record.json`、`review-report.md`、`verification.json`、
`reload-check.json` 和 `manifest.json` 可直接交给同事复核。

## What It Demonstrates

- **Two skewed device clocks**: four shared motion anchors are fitted with a
  least-squares affine mapping. Capture times are projected onto the animation
  reference timeline; residuals are exported as evidence.
- **Missing segment**: device A and device B have different capture windows.
  Their calibrated union leaves one hard gap (about 300 ms). Compression splits
  at that boundary and never interpolates across it.
- **Pausable compression**: work advances in deterministic chunks. A checkpoint
  stores `next_segment`, `next_chunk`, work counters, and the exact selected
  keyframe prefix.
- **Resume equality**: the resumed result is compared with a full uninterrupted
  run. Keyframe sequence, metrics, work counters, and SHA-256 must all match.
- **Resource shortage**: a memory budget selects full, reduced-memory, or
  minimal-memory mode. Degradation changes acceleration, chunk size, and
  tolerance before it ever drops audit evidence.
- **Vulkan + CPU paths**: full mode uses a SPIR-V compute shader for interval
  error scans. Reduced modes and unavailable-device cases use the deterministic
  CPU reference evaluator; the selected backend is recorded.
- **Independent review**: `verify`, `replay`, `compare`, and `reload-check`
  commands operate on the exported artifact. Inputs are copied into the
  artifact directory and referenced by relative paths.
- **Ordered JSON**: objects use insertion-order storage, not a sorted map. A
  reload/reserialize check verifies that fields and top-level order are stable.

## Build

Requirements:

- CMake 3.20+
- C++17 compiler
- Vulkan loader and headers (`libvulkan-dev`)
- GLFW 3.4+ (`libglfw3-dev`)

Ubuntu:

```bash
sudo apt install cmake ninja-build libvulkan-dev libglfw3-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

If you cannot install system packages, the repository has a no-root bootstrap
that extracts Debian development packages into `.deps/root`:

```bash
scripts/bootstrap_deps.sh
cmake -S . -B build -G Ninja
cmake --build build
```

The compiled compute shader is committed as `src/pose_error_spv.inc`; building
does not require `glslangValidator`. The shader source remains in
`shaders/pose_error.comp`; regenerate the header with
`scripts/compile_shader.sh`.

## Run

```bash
./build/skeleton-kf demo --out artifacts/demo \
  --budget-mb 24 --pause-after-chunks 3
```

Add `--window` to create a GLFW window. Without it, GLFW uses its null platform
for deterministic headless processing.

Separate pause/resume workflow:

```bash
./build/skeleton-kf run --out artifacts/run --pause-after-chunks 3
./build/skeleton-kf resume --checkpoint artifacts/run/checkpoint.json \
  --out artifacts/run
```

## Artifact Layout

```text
artifacts/demo/
  review-record.json          machine-readable record and review contract
  review-report.md            colleague-facing narrative
  checkpoint.json             exact paused state
  final-compressed.json       resumed keyframes and metrics
  baseline/final-compressed.json
  baseline/review-record.json
  degraded/final-compressed.json
  degraded/review-record.json
  verification.json           self-verification including strict replay
  reload-check.json           ordered reload proof
  review-record.roundtrip.json
  inputs/                     scenario + both device captures + hashes
  manifest.json               SHA-256 and byte size for every artifact
```

## Reviewer Commands

Run from the artifact directory:

```bash
skeleton-kf verify --record review-record.json
skeleton-kf verify --record review-record.json --strict
skeleton-kf replay --record review-record.json
skeleton-kf reload-check --file review-record.json
skeleton-kf compare --a final-compressed.json --b baseline/final-compressed.json
skeleton-kf inspect --file review-record.json
skeleton-kf manifest --out .
```

- `verify` checks input, evidence, checkpoint, manifest, and result hashes;
  validates selected-index monotonicity; checks the declared field order; and
  recomputes baseline equality.
- `replay` recomputes the clip from the copied inputs with the recorded backend
  and config, then compares the result SHA-256.
- `reload-check` parses and re-serializes the file, then checks byte stability
  and object field order.

## Compression Model

Each source frame contains eight bones. A bone pose is a normalized quaternion
rotation plus a translation in metres. Reconstruction uses shortest-arc
quaternion slerp and linear translation interpolation.

For each continuous segment, frames are processed in fixed chunks. The greedy
inner loop chooses the furthest endpoint whose entire interval stays within:

- angular tolerance in degrees;
- translation tolerance in metres.

An impossible interval advances by one frame and increments `forced_steps`.
Both are exported, so a reviewer can see exactly where quality was sacrificed.

## Resource Modes

The default full working-set estimate depends on fused frame count, not on a
hard-coded amount of host memory.

| Budget | Mode | Acceleration | Chunk | Default tolerances |
|---|---|---|---|---|
| `>= estimate` | `full` | Vulkan compute | 8 intervals | 1.25 deg / 0.012 m |
| `>= 8 MB` | `reduced-memory` | CPU reference | 6 intervals | 2.0 deg / 0.020 m |
| `< 8 MB` | `minimal-memory` | CPU reference | 4 intervals | 3.5 deg / 0.035 m |

The checkpoint, gap boundary, input hashes, backend choice, and result
comparison are retained in every mode.

## Checkpoint Contract

`checkpoint.json` stores the immutable input identity plus:

```text
next_segment, next_chunk, chunks_completed, work_units,
forced_steps, accumulated error bounds, selected_indices
```

Resume validates the scenario/manifest/device hashes and the backend identity
before touching the state. A pause boundary is a completed chunk, so the
selected list is always a valid prefix. The uninterrupted baseline's first
keyframes must equal that prefix, and its complete result hash must equal the
resumed result hash.

## Notes

- The two-device capture is generated deterministically; it is a reproducible
  fixture rather than a live camera/engine integration.
- The GLFW window is a lifecycle/display host. The compression proof is in the
  CLI and artifacts so it works on headless CI and can be replayed by reviewers.
- XML/JSON external dependencies are intentionally absent; ordered JSON, SHA-256,
  and the clip model are implemented in-tree.
