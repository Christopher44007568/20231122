# camtrak — camera trajectory interpolation local-correction tool

Local correction workflow for camera trajectories captured on two devices
whose clocks are not synchronized. Each render object can be re-run on its
own, compared against alternative corrections, and exported into a bundle a
colleague can verify without this repository's runtime context.

Stack: C++17 core, Vulkan + GLFW visualization. No third-party runtime
dependency beyond the vendored loader/GLFW under `third_party/`.

## Quick start

```
cmake -S . -B build -DCAMTRAK_BUILD_GUI=OFF
cmake --build build -j
ctest --test-dir build

./build/camtrak init-demo --project /tmp/demo
./build/camtrak status --project /tmp/demo

# Re-run one render object with a chosen method
./build/camtrak run --project /tmp/demo --object object_alpha \
  --t0 2.0 --t1 3.0 --method linear
./build/camtrak run --project /tmp/demo --object object_alpha \
  --t0 2.0 --t1 3.0 --method cubic

# Compare two runs, verify evidence, export a review bundle
./build/camtrak compare --project /tmp/demo --run-a <id> --run-b <id>
./build/camtrak verify --project /tmp/demo
./build/camtrak export --project /tmp/demo --batch review1 --runs <id1>,<id2>
```

## Interrupted sessions

A crash or Ctrl-C leaves a `running` entry in `session.json`. Re-entering
with the same config reuses the original run id:

```
./build/camtrak resume --project /tmp/demo --object object_alpha
```

## Evidence and review

- `evidence/snapshots/` holds immutable, content-addressed input copies.
- `evidence/runs.log.jsonl` is an append-only hash-chained run record.
- `exports/<batch>/export_manifest.json` lists sha-256 hashes for every
  bundled file, so a reviewer only needs standard hashing tools.

See `docs/DESIGN.md` for the data model and evidence chain, and
`docs/BUILD.md` for the full Vulkan/GLFW build.
